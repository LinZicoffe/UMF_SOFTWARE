#!/usr/bin/env python3
"""UMF RS-485 长时间通信测试上位机。

测试内容:
  1. 轮询一遍全部可读寄存器 (FC03，覆盖只读运行数据区)
  2. 向可写寄存器写入测试值 → 回读 → 逐字校验；再写回原值 → 回读确认已恢复
  3. 回读出错 / 通信失败全部写入日志文件

安全约束 (硬编码，不可绕过):
  - 40127 IAP 升级触发 —— 永不写入 (写 0x5AA5 会让设备复位进 Bootloader)
  - 40092 通信地址 / 40093 波特率配置 —— 不写 (会断开当前连接)
  - 40021~40022 DAC 校准值 —— 默认不写 (影响 4~20mA 输出)，可在界面勾选后参与测试
  - Flash 参数写入按周期节流 (默认 300 s 一轮)，每轮测后立即恢复原值，
    避免高频擦写耗尽 STM32 Flash 寿命 (典型 1 万次)
"""

from __future__ import annotations

import argparse
import queue
import struct
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None
    list_ports = None


# ---------------------------------------------------------------------------
# 寄存器块 —— 与 bsp_usart.c 实际可读路径一致
#   * FC03 单次最多 62 个寄存器
#   * 累积流量 (PDU 40) 必须单独以 start=40 读取
#   * 40019 (PDU 18) 不能独立成块，(0, 18) 覆盖 40001~40018
# ---------------------------------------------------------------------------
READ_BLOCKS = (
    (0, 18),    # 40001~40018：实时测量
    (20, 2),    # 40021~40022：DAC 参数
    (22, 8),    # 40023~40030：运行参数
    (30, 4),    # 40031~40034：量程参数
    (40, 4),    # 40041~40044：累计流量（特殊格式）
    (48, 8),    # 40049~40056：模拟 RAM 参数
    (60, 62),   # 40061~40122：运行状态 + 扩展配置 + 标定
    (122, 4),   # 40123~40126：标定尾部 + 滤波/采样
    (126, 5),   # 40127~40131：IAP/固件信息（只读回显）
)


# ---------------------------------------------------------------------------
# 写测试项
#   addr   — PDU 地址 (Modbus 4X = 40001 + addr)
#   count  — 1 = uint16, 2 = float32
#   storage— "ram" 每轮可测；"flash" 节流测试并恢复原值
#   samples— 安全测试值候选 (均在固件 clamp 范围内，避免"钳位≠回读失败"误判)
#   fc06 / fc10 — 支持的写功能码 (span/DAC 仅 FC10)
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class WriteItem:
    addr: int
    count: int
    name: str
    storage: str
    samples: tuple
    fc06: bool = True
    fc10: bool = True
    group: str = "normal"   # "normal" | "dac"


WRITE_ITEMS: tuple[WriteItem, ...] = (
    # ---- 模拟 RAM 参数 (无 Flash 磨损，每轮写测) ----
    WriteItem(48, 1, "模拟总开关", "ram", (1, 0)),
    WriteItem(50, 2, "模拟瞬时流量", "ram", (12.5, -3.25, 1000.0)),
    WriteItem(52, 2, "模拟温度", "ram", (25.0, -10.5, 0.0)),
    WriteItem(54, 2, "模拟累积流量", "ram", (100.0, 0.001, 9999.75)),

    # ---- 运行参数 (Flash Page 55) ----
    WriteItem(22, 1, "流量单位", "flash", (0, 1, 2, 3)),
    WriteItem(23, 1, "累积单位", "flash", (0, 1, 2, 3)),
    WriteItem(24, 2, "仪表系数", "flash", (0.5, 1.5, 2.0)),
    WriteItem(26, 2, "介质系数", "flash", (0.5, 2.0, 5.0)),
    WriteItem(28, 2, "小信号切除", "flash", (0.0, 5.0, 10.0)),

    # ---- 量程映射 (Flash Page 63，固件仅实现 FC10) ----
    WriteItem(30, 2, "4mA 量程值", "flash", (0.0, -100.0, 50.0), fc06=False),
    WriteItem(32, 2, "20mA 量程值", "flash", (100.0, 200.0, 50.0), fc06=False),

    # ---- 扩展配置 (Flash) ----
    WriteItem(69, 1, "标准工况", "flash", (0, 1, 2)),
    WriteItem(70, 2, "低通时间常数", "flash", (0.5, 2.0, 10.0)),
    WriteItem(72, 2, "阻尼时间", "flash", (0.5, 2.0, 10.0)),
    WriteItem(74, 2, "频率输出", "flash", (100.0, 500.0, 1000.0)),
    WriteItem(76, 1, "脉冲当量", "flash", (0, 1, 2, 3, 4, 5)),
    WriteItem(77, 2, "介质密度", "flash", (999.0, 1.0, 50.0)),
    WriteItem(79, 2, "管径", "flash", (25.0, 50.0, 5.0)),
    WriteItem(81, 2, "气参压力", "flash", (101.3, 50.0, 0.0)),
    WriteItem(83, 2, "气参温度", "flash", (20.0, 0.0, -40.0)),
    WriteItem(85, 2, "雷诺系数", "flash", (1.0, 0.5, 10.0)),
    WriteItem(87, 2, "累积系数", "flash", (1.0, 2.0, 0.5)),
    WriteItem(89, 2, "预设总量", "flash", (0.0, 100.0, 9999999.0)),
    WriteItem(93, 1, "语言", "flash", (0,)),          # 固件仅接受 0
    WriteItem(94, 1, "OLED 自愈间隔", "flash", (50, 100, 0)),
    WriteItem(95, 1, "标定使能", "flash", (0, 1)),
    WriteItem(96, 2, "标定 k[0]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(98, 2, "标定 k[1]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(100, 2, "标定 k[2]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(102, 2, "标定 k[3]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(104, 2, "标定 k[4]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(106, 2, "标定 k[5]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(108, 2, "标定 k[6]", "flash", (1.0, 1.2, 0.8)),
    WriteItem(110, 2, "标定 pct[0]", "flash", (0.0, 5.0)),
    WriteItem(112, 2, "标定 pct[1]", "flash", (3.0, 8.0)),
    WriteItem(114, 2, "标定 pct[2]", "flash", (10.0, 15.0)),
    WriteItem(116, 2, "标定 pct[3]", "flash", (25.0, 30.0)),
    WriteItem(118, 2, "标定 pct[4]", "flash", (50.0, 55.0)),
    WriteItem(120, 2, "标定 pct[5]", "flash", (75.0, 80.0)),
    WriteItem(122, 2, "标定 pct[6]", "flash", (100.0, 95.0)),
    WriteItem(124, 1, "滑动窗口点数", "flash", (2, 5, 10)),
    WriteItem(125, 1, "采样间隔", "flash", (500, 1000, 200)),

    # ---- DAC 校准 (Flash Page 59，需勾选后才测；影响 4~20mA 输出) ----
    WriteItem(20, 1, "DAC 零点", "flash", (100, 200), fc06=False, group="dac"),
    WriteItem(21, 1, "DAC 满度", "flash", (800, 900), fc06=False, group="dac"),
)


# ---------------------------------------------------------------------------
# Modbus RTU 基础
# ---------------------------------------------------------------------------
def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def f32_to_regs(value: float) -> tuple[int, int]:
    """float32 → 两个 Modbus 寄存器 (低位字在前，与固件 str[0..3] 布局一致)。"""
    raw = struct.pack("<f", value)
    reg_lo = (raw[1] << 8) | raw[0]
    reg_hi = (raw[3] << 8) | raw[2]
    return reg_lo, reg_hi


def sample_to_regs(item: WriteItem, sample) -> tuple[int, ...]:
    if item.count == 1:
        return (int(sample) & 0xFFFF,)
    return f32_to_regs(float(sample))


def regs_to_text(regs: tuple[int, ...]) -> str:
    return ",".join(f"0x{r:04X}" for r in regs)


def read_exact(port, size: int) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + port.timeout
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def _frame(body: bytes) -> bytes:
    crc = crc16_modbus(body)
    return body + bytes((crc & 0xFF, crc >> 8))


def read_holding_registers(port, slave: int, start: int, count: int) -> list[int]:
    request = _frame(bytes((slave, 0x03, start >> 8, start & 0xFF, count >> 8, count & 0xFF)))
    port.reset_input_buffer()
    port.write(request)
    port.flush()

    header = read_exact(port, 3)
    if len(header) != 3:
        raise TimeoutError(f"读 {40001 + start} 超时")
    if header[0] != slave:
        raise ValueError(f"从站地址不符：收到 {header[0]}")
    if header[1] == 0x83:
        tail = read_exact(port, 2)
        raise ValueError(f"读 {40001 + start} 异常码 0x{header[2]:02X}，CRC={tail.hex(' ')}")
    if header[1] != 0x03 or header[2] != count * 2:
        raise ValueError(f"读 {40001 + start} 响应格式错误：{header.hex(' ')}")

    tail = read_exact(port, header[2] + 2)
    frame = header + tail
    if len(tail) != header[2] + 2:
        raise TimeoutError(f"读 {40001 + start} 响应不完整")
    received_crc = frame[-2] | (frame[-1] << 8)
    if crc16_modbus(frame[:-2]) != received_crc:
        raise ValueError(f"读 {40001 + start} CRC 错误")

    payload = frame[3:-2]
    return [(payload[i] << 8) | payload[i + 1] for i in range(0, len(payload), 2)]


def write_single_register(port, slave: int, addr: int, value: int) -> None:
    request = _frame(bytes((slave, 0x06, addr >> 8, addr & 0xFF, value >> 8, value & 0xFF)))
    port.reset_input_buffer()
    port.write(request)
    port.flush()
    resp = read_exact(port, 8)
    if len(resp) != 8:
        raise TimeoutError(f"FC06 写 {40001 + addr} 无响应")
    if resp[:6] != request[:6]:
        raise ValueError(f"FC06 写 {40001 + addr} 回显不符：{resp.hex(' ')}")
    if crc16_modbus(resp[:6]) != (resp[6] | (resp[7] << 8)):
        raise ValueError(f"FC06 写 {40001 + addr} 响应 CRC 错误")


def write_multiple_registers(port, slave: int, addr: int, values: tuple[int, ...]) -> None:
    count = len(values)
    payload = b"".join(v.to_bytes(2, "big") for v in values)
    body = bytes((slave, 0x10, addr >> 8, addr & 0xFF, count >> 8, count & 0xFF, count * 2)) + payload
    request = _frame(body)
    port.reset_input_buffer()
    port.write(request)
    port.flush()
    resp = read_exact(port, 8)
    if len(resp) != 8:
        raise TimeoutError(f"FC10 写 {40001 + addr} 无响应")
    if resp[1] != 0x10:
        raise ValueError(f"FC10 写 {40001 + addr} 功能码异常：{resp.hex(' ')}")
    if resp[2:6] != request[2:6]:
        raise ValueError(f"FC10 写 {40001 + addr} 回显不符：{resp.hex(' ')}")
    if crc16_modbus(resp[:6]) != (resp[6] | (resp[7] << 8)):
        raise ValueError(f"FC10 写 {40001 + addr} 响应 CRC 错误")


def write_regs(port, slave: int, item: WriteItem, regs: tuple[int, ...], use_fc10: bool) -> str:
    """写入寄存器组，返回实际使用的功能码标签。"""
    if item.count == 1:
        if item.fc06:
            write_single_register(port, slave, item.addr, regs[0])
            return "FC06"
        # DAC 等仅实现 FC10 的寄存器
        write_multiple_registers(port, slave, item.addr, regs)
        return "FC10"
    if use_fc10 and item.fc10:
        write_multiple_registers(port, slave, item.addr, regs)
        return "FC10"
    if item.fc06:
        # float 分次写入：先低位字，再高位字（固件在高位字到达时提交）
        write_single_register(port, slave, item.addr, regs[0])
        time.sleep(0.01)
        write_single_register(port, slave, item.addr + 1, regs[1])
        return "FC06×2"
    write_multiple_registers(port, slave, item.addr, regs)
    return "FC10"


# ---------------------------------------------------------------------------
# 测试统计 / 日志
# ---------------------------------------------------------------------------
@dataclass
class Stats:
    read_ok: int = 0
    read_fail: int = 0
    write_ok: int = 0
    write_fail: int = 0
    verify_fail: int = 0
    restore_fail: int = 0
    cycles: int = 0
    flash_rounds: int = 0

    def summary(self) -> str:
        return (
            f"轮次 {self.cycles} | 读成功 {self.read_ok} 读失败 {self.read_fail} | "
            f"写成功 {self.write_ok} 写失败 {self.write_fail} | "
            f"回读错误 {self.verify_fail} 恢复失败 {self.restore_fail} | "
            f"Flash 轮 {self.flash_rounds}"
        )


class SessionLog:
    def __init__(self, path: Path):
        self.path = path
        self._lock = threading.Lock()
        with self.path.open("w", encoding="utf-8") as fp:
            fp.write(f"# UMF RS-485 长时间通信测试日志  {datetime.now().isoformat(timespec='seconds')}\n")

    def write(self, level: str, message: str) -> None:
        line = f"{datetime.now().isoformat(timespec='seconds')} [{level}] {message}\n"
        with self._lock:
            with self.path.open("a", encoding="utf-8") as fp:
                fp.write(line)


# ---------------------------------------------------------------------------
# 测试工作线程
# ---------------------------------------------------------------------------
@dataclass
class TestConfig:
    port_name: str
    baudrate: int
    slave: int
    duration_s: float          # 0 = 不限时
    interval_s: float
    do_read_poll: bool
    do_write_test: bool
    write_flash: bool
    include_dac: bool
    flash_period_s: float


class TestWorker:
    def __init__(self, cfg: TestConfig, log: SessionLog, events: queue.Queue):
        self.cfg = cfg
        self.log = log
        self.events = events
        self.stop_event = threading.Event()
        self.stats = Stats()
        self.started_at = 0.0
        self._cycle_index = 0

    # ---- 对外 ----
    def run(self) -> None:
        self.started_at = time.monotonic()
        last_flash = 0.0
        try:
            with serial.Serial(
                self.cfg.port_name,
                self.cfg.baudrate,
                bytesize=8,
                parity=serial.PARITY_NONE,
                stopbits=1,
                timeout=0.5,
                write_timeout=0.5,
            ) as port:
                while not self.stop_event.is_set():
                    cycle_start = time.monotonic()
                    elapsed = cycle_start - self.started_at
                    if self.cfg.duration_s > 0 and elapsed >= self.cfg.duration_s:
                        break
                    self._cycle_index += 1
                    self.stats.cycles = self._cycle_index

                    flash_due = (
                        self.cfg.do_write_test
                        and self.cfg.write_flash
                        and (self._cycle_index == 1
                             or cycle_start - last_flash >= self.cfg.flash_period_s)
                    )
                    if flash_due:
                        last_flash = cycle_start
                        self.stats.flash_rounds += 1

                    if self.cfg.do_read_poll:
                        self._poll_readable(port)
                    if self.cfg.do_write_test:
                        self._write_verify_round(port, flash_due)

                    self.events.put(("stats", self.stats.summary()))
                    wait = max(0.05, self.cfg.interval_s - (time.monotonic() - cycle_start))
                    self.stop_event.wait(wait)
        except Exception as exc:
            self._log_error(f"测试线程异常终止: {exc}")
            self.events.put(("fatal", str(exc)))
        finally:
            total = int(self.elapsed())
            summary = (
                f"{self._runtime_tag()} 总运行时长 "
                f"{total // 3600:02d}:{(total % 3600) // 60:02d}:{total % 60:02d} | "
                f"{self.stats.summary()}"
            )
            self.log.write("INFO", f"测试结束 {summary}")
            self.events.put(("done", (self.stats.summary(), float(total))))

    def request_stop(self) -> None:
        self.stop_event.set()

    def elapsed(self) -> float:
        return time.monotonic() - self.started_at if self.started_at else 0.0

    def _runtime_tag(self) -> str:
        """T+ 运行时长标签，便于日志与「总运行时长」对齐。"""
        total = int(self.elapsed())
        return f"T+{total // 3600:02d}:{(total % 3600) // 60:02d}:{total % 60:02d}"

    def _log_error(self, message: str) -> None:
        line = f"{self._runtime_tag()} {message}"
        self.log.write("ERROR", line)
        self.events.put(("log", f"[ERROR] {line}"))

    # ---- 只读轮询 ----
    def _poll_readable(self, port) -> None:
        for start, count in READ_BLOCKS:
            if self.stop_event.is_set():
                return
            try:
                read_holding_registers(port, self.cfg.slave, start, count)
                self.stats.read_ok += 1
                time.sleep(0.02)
            except Exception as exc:
                self.stats.read_fail += 1
                end = 40001 + start + count - 1
                self._log_error(f"只读轮询失败 {40001 + start}~{end}: {exc}")

    # ---- 写入回读 ----
    def _write_verify_round(self, port, flash_due: bool) -> None:
        for item in WRITE_ITEMS:
            if self.stop_event.is_set():
                return
            if item.group == "dac" and not self.cfg.include_dac:
                continue
            if item.storage == "flash" and not (self.cfg.write_flash and flash_due):
                continue
            try:
                self._verify_item(port, item)
            except Exception as exc:
                self.stats.write_fail += 1
                self._log_error(f"{item.name}({40001 + item.addr}) 写测异常: {exc}")
            time.sleep(0.02)

    def _read_item_regs(self, port, item: WriteItem) -> tuple[int, ...]:
        return tuple(read_holding_registers(port, self.cfg.slave, item.addr, item.count))

    def _verify_item(self, port, item: WriteItem) -> None:
        """写测试值 → 回读校验 → 恢复原值 → 回读确认。异常时 finally 保证恢复。"""
        original = self._read_item_regs(port, item)
        try:
            sample = item.samples[(self._cycle_index - 1) % len(item.samples)]
            expect = sample_to_regs(item, sample)
            use_fc10 = (self._cycle_index % 2 == 1)   # 交替 FC10 / FC06，覆盖两条写路径

            tag = write_regs(port, self.cfg.slave, item, expect, use_fc10)
            self.stats.write_ok += 1
            time.sleep(0.02)
            got = self._read_item_regs(port, item)
            if got != expect:
                self.stats.verify_fail += 1
                self._log_error(
                    f"回读不一致 {item.name}({40001 + item.addr}) [{tag}] "
                    f"写入={regs_to_text(expect)} 回读={regs_to_text(got)} "
                    f"原值={regs_to_text(original)} 样本={sample} 轮次={self._cycle_index}"
                )
        finally:
            # 立即恢复原值并确认（缩短参数被占用窗口，降低 Flash 磨损与副作用）
            try:
                write_regs(port, self.cfg.slave, item, original, use_fc10=True)
                time.sleep(0.02)
                got2 = self._read_item_regs(port, item)
                if got2 != original:
                    self.stats.restore_fail += 1
                    self._log_error(
                        f"恢复失败 {item.name}({40001 + item.addr}) "
                        f"期望原值={regs_to_text(original)} 实际={regs_to_text(got2)} "
                        f"轮次={self._cycle_index}"
                    )
            except Exception as restore_exc:
                self.stats.restore_fail += 1
                self._log_error(f"{item.name}({40001 + item.addr}) 恢复原值异常: {restore_exc}")


# ---------------------------------------------------------------------------
# 图形界面
# ---------------------------------------------------------------------------
class LongRunApp:
    def __init__(self, root: tk.Tk, args: argparse.Namespace):
        self.root = root
        self.events: queue.Queue = queue.Queue()
        self.worker: TestWorker | None = None
        self.log: SessionLog | None = None
        self.log_path: Path | None = None
        self.testing = False

        root.title("UMF RS-485 长时间通信测试")
        root.geometry("920x640")
        root.minsize(780, 520)

        self._build_toolbar(args)
        self._build_status()
        self._build_log_view()
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(100, self.process_events)

    # ---- 界面搭建 ----
    def _build_toolbar(self, args: argparse.Namespace) -> None:
        top = ttk.Frame(self.root, padding=8)
        top.pack(fill=tk.X)

        ttk.Label(top, text="串口").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=args.port)
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=10)
        self.port_combo.pack(side=tk.LEFT, padx=(4, 4))
        self._refresh_ports()
        ttk.Button(top, text="刷新", command=self._refresh_ports, width=6).pack(side=tk.LEFT)

        ttk.Label(top, text="波特率").pack(side=tk.LEFT, padx=(12, 0))
        self.baud_var = tk.StringVar(value=str(args.baud))
        ttk.Combobox(
            top, textvariable=self.baud_var, width=8, state="readonly",
            values=("2400", "4800", "9600", "19200", "38400", "115200"),
        ).pack(side=tk.LEFT, padx=(4, 0))

        ttk.Label(top, text="从站").pack(side=tk.LEFT, padx=(12, 0))
        self.slave_var = tk.StringVar(value=str(args.slave))
        ttk.Spinbox(top, from_=1, to=247, textvariable=self.slave_var, width=5).pack(
            side=tk.LEFT, padx=(4, 0)
        )

        mid = ttk.Frame(self.root, padding=(8, 0))
        mid.pack(fill=tk.X)

        ttk.Label(mid, text="总运行时长(分)").pack(side=tk.LEFT)
        self.duration_var = tk.StringVar(value=str(args.duration))
        self.duration_var.trace_add("write", self._on_duration_changed)
        ttk.Spinbox(
            mid, from_=0, to=99999, textvariable=self.duration_var, width=7
        ).pack(side=tk.LEFT, padx=(4, 4))
        ttk.Label(mid, text="(0=不限时, 运行中可改)").pack(side=tk.LEFT)

        ttk.Label(mid, text="轮询间隔(秒)").pack(side=tk.LEFT, padx=(12, 0))
        self.interval_var = tk.StringVar(value=str(args.interval))
        ttk.Spinbox(mid, from_=0, to=60, increment=0.5, textvariable=self.interval_var, width=6).pack(
            side=tk.LEFT, padx=(4, 4)
        )

        ttk.Label(mid, text="Flash 写测周期(秒)").pack(side=tk.LEFT, padx=(12, 0))
        self.flash_period_var = tk.StringVar(value=str(args.flash_period))
        ttk.Spinbox(mid, from_=0, to=3600, increment=30, textvariable=self.flash_period_var, width=6).pack(
            side=tk.LEFT, padx=(4, 0)
        )

        opt = ttk.Frame(self.root, padding=(8, 4))
        opt.pack(fill=tk.X)
        self.read_poll_var = tk.BooleanVar(value=True)
        self.write_test_var = tk.BooleanVar(value=True)
        self.write_flash_var = tk.BooleanVar(value=True)
        self.include_dac_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(opt, text="只读轮询", variable=self.read_poll_var).pack(side=tk.LEFT)
        ttk.Checkbutton(opt, text="写入回读", variable=self.write_test_var).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Checkbutton(opt, text="写 Flash 参数", variable=self.write_flash_var).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Checkbutton(opt, text="含 DAC 校准寄存器", variable=self.include_dac_var).pack(
            side=tk.LEFT, padx=(8, 0)
        )

        btns = ttk.Frame(self.root, padding=(8, 4))
        btns.pack(fill=tk.X)
        self.start_btn = ttk.Button(btns, text="开始测试", command=self.start_test)
        self.start_btn.pack(side=tk.LEFT)
        self.stop_btn = ttk.Button(btns, text="停止测试", command=self.stop_test, state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=(8, 0))
        ttk.Label(
            btns,
            text="永不写 40127(IAP)/40092(地址)/40093(波特率)；Flash 参数测后立即恢复原值",
            foreground="#666666",
        ).pack(side=tk.LEFT, padx=16)

        prog = ttk.Frame(self.root, padding=(8, 0))
        prog.pack(fill=tk.X)
        self.progress = ttk.Progressbar(prog, mode="determinate", maximum=100)
        self.progress.pack(fill=tk.X, side=tk.LEFT, expand=True)
        self.time_var = tk.StringVar(value="总运行时长 00:00:00  剩余 --:--:--")
        ttk.Label(prog, textvariable=self.time_var, width=32).pack(side=tk.LEFT, padx=(8, 0))

    def _build_status(self) -> None:
        frame = ttk.Frame(self.root, padding=(8, 4))
        frame.pack(fill=tk.X)
        self.stats_var = tk.StringVar(value=Stats().summary())
        ttk.Label(frame, textvariable=self.stats_var).pack(side=tk.LEFT)
        self.link_var = tk.StringVar(value="状态: 就绪")
        self.link_label = tk.Label(frame, textvariable=self.link_var, fg="#008000")
        self.link_label.pack(side=tk.RIGHT)

    def _build_log_view(self) -> None:
        frame = ttk.Frame(self.root, padding=8)
        frame.pack(fill=tk.BOTH, expand=True)
        self.log_text = tk.Text(frame, height=18, wrap=tk.WORD, state=tk.DISABLED)
        scroll = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=scroll.set)
        self.log_text.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        self.log_text.tag_configure("error", foreground="#c00000")
        self.log_text.tag_configure("info", foreground="#004488")
        self.path_var = tk.StringVar(value="日志文件: —")
        ttk.Label(self.root, textvariable=self.path_var, padding=(8, 0, 8, 6)).pack(fill=tk.X)

    # ---- 辅助 ----
    def _on_duration_changed(self, *_args) -> None:
        """总运行时长运行中可调：同步到工作线程，立即影响截止判断与进度条。"""
        if self.worker is None:
            return
        try:
            minutes = float(self.duration_var.get())
        except ValueError:
            return
        self.worker.cfg.duration_s = max(0.0, minutes) * 60.0

    def _refresh_ports(self) -> None:
        names = [p.device for p in list_ports.comports()] if list_ports is not None else []
        current = self.port_var.get().strip()
        if current and current not in names:
            names.insert(0, current)
        self.port_combo["values"] = names

    def _append_log(self, message: str, tag: str = "") -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, f"[{stamp}] {message}\n", tag)
        self.log_text.see(tk.END)
        # 限制界面日志行数，避免长时间测试占满内存
        lines = int(self.log_text.index("end-1c").split(".")[0])
        if lines > 2000:
            self.log_text.delete("1.0", f"{lines - 1500}.0")
        self.log_text.configure(state=tk.DISABLED)

    @staticmethod
    def _fmt_duration(seconds: float) -> str:
        seconds = max(0, int(seconds))
        return f"{seconds // 3600:02d}:{(seconds % 3600) // 60:02d}:{seconds % 60:02d}"

    # ---- 测试控制 ----
    def start_test(self) -> None:
        if serial is None:
            messagebox.showerror("缺少依赖", "请先执行：python -m pip install pyserial")
            return
        port_name = self.port_var.get().strip()
        if not port_name:
            messagebox.showerror("设置错误", "请选择或输入串口名称。")
            return
        try:
            baudrate = int(self.baud_var.get())
            slave = int(self.slave_var.get())
            duration_min = float(self.duration_var.get())
            interval_s = float(self.interval_var.get())
            flash_period_s = float(self.flash_period_var.get())
        except ValueError:
            messagebox.showerror("设置错误", "时长 / 间隔 / 从站必须是数字。")
            return
        if not 1 <= slave <= 247:
            messagebox.showerror("设置错误", "从站地址必须在 1～247 之间。")
            return
        if not self.read_poll_var.get() and not self.write_test_var.get():
            messagebox.showerror("设置错误", "至少勾选一项测试内容（只读轮询 / 写入回读）。")
            return
        if self.write_test_var.get() and self.write_flash_var.get() and flash_period_s < 30:
            if not messagebox.askyesno(
                "Flash 磨损提醒",
                "Flash 参数写测周期小于 30 秒会显著消耗擦写寿命（约 1 万次）。\n"
                "仍要以当前周期开始吗？",
            ):
                return

        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.log_path = Path(__file__).resolve().parent / f"rs485_longrun_{stamp}.log"
        self.log = SessionLog(self.log_path)
        self.path_var.set(f"日志文件: {self.log_path.name}")

        cfg = TestConfig(
            port_name=port_name,
            baudrate=baudrate,
            slave=slave,
            duration_s=max(0.0, duration_min) * 60.0,
            interval_s=max(0.0, interval_s),
            do_read_poll=self.read_poll_var.get(),
            do_write_test=self.write_test_var.get(),
            write_flash=self.write_flash_var.get(),
            include_dac=self.include_dac_var.get(),
            flash_period_s=max(1.0, flash_period_s),
        )
        self.log.write(
            "INFO",
            f"开始测试 port={port_name} baud={baudrate} slave={slave} "
            f"总运行时长={'不限时' if cfg.duration_s == 0 else f'{duration_min} 分钟'} "
            f"interval={interval_s}s flash_period={cfg.flash_period_s}s "
            f"read_poll={cfg.do_read_poll} write_test={cfg.do_write_test} "
            f"write_flash={cfg.write_flash} include_dac={cfg.include_dac}",
        )
        self._append_log(f"[INFO] 开始测试 → {self.log_path.name}", "info")

        self.worker = TestWorker(cfg, self.log, self.events)
        self.testing = True
        self.start_btn.configure(state=tk.DISABLED)
        self.stop_btn.configure(state=tk.NORMAL)
        self.link_var.set("状态: 测试中")
        self.link_label.configure(fg="#b07000")
        threading.Thread(target=self.worker.run, daemon=True).start()

    def stop_test(self) -> None:
        if self.worker is not None:
            self.worker.request_stop()
            self._append_log("[INFO] 正在停止……(写测项会先恢复原值)", "info")

    def process_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "stats":
                    self.stats_var.set(str(payload))
                elif kind == "log":
                    text = str(payload)
                    tag = "error" if text.startswith("[ERROR]") else ""
                    self._append_log(text, tag)
                elif kind == "fatal":
                    self._append_log(f"[ERROR] 测试异常终止: {payload}", "error")
                    self.link_var.set("状态: 异常终止")
                    self.link_label.configure(fg="#c00000")
                elif kind == "done":
                    self.stats_var.set(str(payload[0]))
                    self._finish(str(payload[0]), float(payload[1]))
        except queue.Empty:
            pass

        if self.testing and self.worker is not None:
            self._update_runtime(self.worker.elapsed(), self.worker.cfg.duration_s)

        # 事件泵常驻，保证二次启动测试仍能刷新界面
        self.root.after(100, self.process_events)

    def _update_runtime(self, elapsed: float, duration: float) -> None:
        """刷新总运行时长 / 剩余时间 / 进度条。"""
        if duration > 0:
            remain = max(0.0, duration - elapsed)
            self.progress["value"] = min(100.0, elapsed / duration * 100.0)
            self.time_var.set(
                f"总运行时长 {self._fmt_duration(elapsed)}  剩余 {self._fmt_duration(remain)}"
            )
        else:
            self.progress["value"] = 0
            self.time_var.set(
                f"总运行时长 {self._fmt_duration(elapsed)}  剩余 --:--:--"
            )

    def _finish(self, summary: str, elapsed: float = 0.0) -> None:
        # 定格最终总运行时长
        duration = self.worker.cfg.duration_s if self.worker is not None else 0.0
        self._update_runtime(elapsed, duration)
        if self.log is not None:
            self.log.write("INFO", f"测试结束 总运行时长 {self._fmt_duration(elapsed)} | {summary}")
        self._append_log(
            f"[INFO] 测试结束 总运行时长 {self._fmt_duration(elapsed)} | {summary}", "info"
        )
        self.testing = False
        self.worker = None
        self.start_btn.configure(state=tk.NORMAL)
        self.stop_btn.configure(state=tk.DISABLED)
        self.link_var.set("状态: 已停止")
        self.link_label.configure(fg="#777777")

    def close(self) -> None:
        if self.testing and self.worker is not None:
            if not messagebox.askokcancel("退出", "测试仍在进行，确定退出并停止测试吗？"):
                return
            self.worker.request_stop()
            # 给工作线程一点时间写回原值
            self.worker.stop_event.wait(2.0)
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="UMF RS-485 长时间通信测试")
    parser.add_argument("--port", default="COM8", help="串口，默认 COM8")
    parser.add_argument("--baud", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument("--slave", type=int, default=2, help="Modbus 从站地址，默认 2")
    parser.add_argument("--duration", type=float, default=60, help="总运行时长（分钟），0=不限时，默认 60")
    parser.add_argument("--interval", type=float, default=1.0, help="轮询间隔（秒），默认 1")
    parser.add_argument("--flash-period", type=float, default=300.0, help="Flash 参数写测周期（秒），默认 300")
    args = parser.parse_args()

    root = tk.Tk()
    if serial is None:
        root.withdraw()
        messagebox.showerror("缺少依赖", "请先执行：python -m pip install pyserial")
        root.destroy()
        return

    LongRunApp(root, args)
    root.mainloop()


if __name__ == "__main__":
    main()
