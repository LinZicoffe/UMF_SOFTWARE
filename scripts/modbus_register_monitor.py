#!/usr/bin/env python3
"""UMF Modbus RTU 寄存器上下电变化监视器。"""

from __future__ import annotations

import argparse
import csv
import queue
import threading
import time
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


# 固件要求累计流量必须从地址 40 单独读取，且单次 FC03 最多读取 62 个寄存器。
READ_BLOCKS = (
    (0, 18),    # 40001~40018：实时测量（40019 为不可独立读取的保留地址）
    (20, 2),    # 40021~40022：DAC FLASH 参数
    (22, 8),    # 40023~40030：运行 FLASH 参数
    (30, 4),    # 40031~40034：量程 FLASH 参数
    (40, 4),    # 40041~40044：累计流量特殊格式
    (48, 8),    # 40049~40056：模拟 RAM 参数
    (60, 62),   # 40061~40122：运行状态和 FLASH 参数
    (122, 4),   # 40123~40126：FLASH 参数
    (126, 5),   # 40127~40131：IAP/固件信息
)


def is_flash_parameter(address: int) -> bool:
    """返回该 PDU 地址是否对应参数存储区。"""
    return 20 <= address <= 33 or 69 <= address <= 125


def area_name(address: int) -> str:
    if is_flash_parameter(address):
        return "FLASH参数"
    if 48 <= address <= 55:
        return "模拟RAM"
    if 126 <= address <= 130:
        return "固件信息"
    return "运行数据"


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def read_exact(port, size: int) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + port.timeout
    while len(data) < size and time.monotonic() < deadline:
        chunk = port.read(size - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_holding_registers(port, slave: int, start: int, count: int) -> list[int]:
    body = bytes((slave, 0x03, start >> 8, start & 0xFF, count >> 8, count & 0xFF))
    crc = crc16_modbus(body)
    request = body + bytes((crc & 0xFF, crc >> 8))

    port.reset_input_buffer()
    port.write(request)
    port.flush()

    header = read_exact(port, 3)
    if len(header) != 3:
        raise TimeoutError(f"读取 {40001 + start} 超时")
    if header[0] != slave:
        raise ValueError(f"从站地址不符：收到 {header[0]}")
    if header[1] == 0x83:
        tail = read_exact(port, 2)
        end = 40001 + start + count - 1
        raise ValueError(
            f"读取 {40001 + start}~{end} 时收到 Modbus 异常码 "
            f"0x{header[2]:02X}，CRC={tail.hex(' ')}"
        )
    if header[1] != 0x03 or header[2] != count * 2:
        raise ValueError(f"响应格式错误：{header.hex(' ')}")

    tail = read_exact(port, header[2] + 2)
    frame = header + tail
    if len(tail) != header[2] + 2:
        raise TimeoutError(f"读取 {40001 + start} 响应不完整")
    received_crc = frame[-2] | (frame[-1] << 8)
    if crc16_modbus(frame[:-2]) != received_crc:
        raise ValueError(f"读取 {40001 + start} 时 CRC 错误")

    payload = frame[3:-2]
    return [(payload[i] << 8) | payload[i + 1] for i in range(0, len(payload), 2)]


class RegisterMonitor:
    def __init__(self, root: tk.Tk, port_name: str, baudrate: int, slave: int, interval: float):
        self.root = root
        self.port_name = port_name
        self.baudrate = baudrate
        self.slave = slave
        self.interval = interval
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.stop_event = threading.Event()
        self.reconnect_event = threading.Event()
        self.baseline: dict[int, int] | None = None
        self.current: dict[int, int] = {}
        self.last_logged: dict[int, int] = {}
        self.log_path: Path | None = None
        self.successful_cycles = 0

        root.title("UMF 寄存器上下电变化监视器")
        root.geometry("1000x720")

        toolbar = ttk.Frame(root, padding=8)
        toolbar.pack(fill=tk.X)
        ttk.Label(toolbar, text="串口").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value=port_name)
        self.port_combo = ttk.Combobox(toolbar, textvariable=self.port_var, width=10)
        self.port_combo.pack(side=tk.LEFT, padx=(4, 8))
        self.refresh_ports()
        ttk.Button(toolbar, text="刷新", command=self.refresh_ports).pack(side=tk.LEFT)

        ttk.Label(toolbar, text="从站").pack(side=tk.LEFT, padx=(12, 0))
        self.slave_var = tk.StringVar(value=str(slave))
        ttk.Spinbox(toolbar, from_=1, to=247, textvariable=self.slave_var, width=5).pack(
            side=tk.LEFT, padx=(4, 8)
        )
        ttk.Button(toolbar, text="应用连接", command=self.apply_connection).pack(side=tk.LEFT)
        ttk.Label(toolbar, text=f"{baudrate} 8N1").pack(side=tk.LEFT, padx=(12, 0))
        self.link_label = tk.Label(toolbar, text="● 尚未收到有效数据", fg="#777777")
        self.link_label.pack(side=tk.LEFT, padx=12)
        ttk.Button(toolbar, text="记录基准", command=self.record_baseline).pack(side=tk.RIGHT)
        ttk.Button(toolbar, text="清除基准", command=self.clear_baseline).pack(side=tk.RIGHT, padx=8)

        self.status = tk.StringVar(value="正在连接……")
        ttk.Label(root, textvariable=self.status, padding=(8, 0, 8, 8)).pack(fill=tk.X)

        columns = ("address", "area", "current", "baseline", "change")
        self.table = ttk.Treeview(root, columns=columns, show="headings")
        headings = {
            "address": "寄存器",
            "area": "区域",
            "current": "当前值",
            "baseline": "记录值",
            "change": "变化",
        }
        widths = {"address": 100, "area": 110, "current": 130, "baseline": 130, "change": 180}
        for name in columns:
            self.table.heading(name, text=headings[name])
            self.table.column(name, width=widths[name], anchor=tk.CENTER)
        self.table.tag_configure("changed", foreground="#d00000", background="#fff0f0")
        self.table.tag_configure("flash_changed", foreground="#ffffff", background="#c00000")

        scrollbar = ttk.Scrollbar(root, orient=tk.VERTICAL, command=self.table.yview)
        self.table.configure(yscrollcommand=scrollbar.set)
        self.table.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(8, 0), pady=(0, 8))
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y, padx=(0, 8), pady=(0, 8))

        for start, count in READ_BLOCKS:
            for address in range(start, start + count):
                self.table.insert(
                    "", tk.END, iid=str(address),
                    values=(40001 + address, area_name(address), "—", "—", ""),
                )

        self.worker = threading.Thread(target=self.poll_loop, daemon=True)
        self.worker.start()
        root.after(100, self.process_events)
        root.protocol("WM_DELETE_WINDOW", self.close)

    def poll_loop(self) -> None:
        port = None
        while not self.stop_event.is_set():
            cycle_started = time.monotonic()
            try:
                if self.reconnect_event.is_set():
                    if port is not None:
                        port.close()
                        port = None
                    self.reconnect_event.clear()
                if port is None or not port.is_open:
                    port = serial.Serial(
                        self.port_name,
                        self.baudrate,
                        bytesize=8,
                        parity=serial.PARITY_NONE,
                        stopbits=1,
                        timeout=0.5,
                        write_timeout=0.5,
                    )
                values: dict[int, int] = {}
                for start, count in READ_BLOCKS:
                    block = read_holding_registers(port, self.slave, start, count)
                    values.update({start + index: value for index, value in enumerate(block)})
                    time.sleep(0.02)
                self.events.put(("values", values))
            except Exception as exc:
                if port is not None:
                    try:
                        port.close()
                    except Exception:
                        pass
                    port = None
                self.events.put(("error", str(exc)))

            elapsed = time.monotonic() - cycle_started
            self.stop_event.wait(max(0.1, self.interval - elapsed))

        if port is not None:
            port.close()

    def refresh_ports(self) -> None:
        names = [item.device for item in list_ports.comports()] if list_ports is not None else []
        current = self.port_var.get().strip()
        if current and current not in names:
            names.insert(0, current)
        self.port_combo["values"] = names

    def apply_connection(self) -> None:
        port_name = self.port_var.get().strip()
        try:
            slave = int(self.slave_var.get())
        except ValueError:
            messagebox.showerror("设置错误", "从站地址必须是 1～247 的整数。")
            return
        if not port_name:
            messagebox.showerror("设置错误", "请选择或输入串口名称。")
            return
        if not 1 <= slave <= 247:
            messagebox.showerror("设置错误", "从站地址必须在 1～247 之间。")
            return

        self.port_name = port_name
        self.slave = slave
        self.baseline = None
        self.current.clear()
        self.last_logged.clear()
        self.log_path = None
        self.successful_cycles = 0
        self.reconnect_event.set()
        self.link_label.config(text="● 正在连接", fg="#b07000")
        self.status.set(f"正在连接 {port_name}，从站 {slave}……")

    def process_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "values":
                    self.update_values(payload)
                else:
                    self.link_label.config(text="● 未收到数据", fg="#c00000")
                    self.status.set(f"通信中断，正在自动重试：{payload}")
        except queue.Empty:
            pass
        if not self.stop_event.is_set():
            self.root.after(100, self.process_events)

    def update_values(self, values: dict[int, int]) -> None:
        self.current = values
        self.successful_cycles += 1
        self.link_label.config(text="● 已收到有效数据", fg="#008000")
        flash_changes = 0
        all_changes = 0
        for address, value in values.items():
            baseline_value = self.baseline.get(address) if self.baseline is not None else None
            changed = baseline_value is not None and value != baseline_value
            if changed:
                all_changes += 1
                flash_changes += int(is_flash_parameter(address))
                change_text = f"{baseline_value} → {value}"
                tag = "flash_changed" if is_flash_parameter(address) else "changed"
                self.log_change(address, baseline_value, value)
            else:
                change_text = ""
                tag = ""
            self.table.item(
                str(address),
                values=(
                    40001 + address,
                    area_name(address),
                    f"{value}  (0x{value:04X})",
                    "—" if baseline_value is None else f"{baseline_value}  (0x{baseline_value:04X})",
                    change_text,
                ),
                tags=(tag,) if tag else (),
            )

        now = datetime.now().strftime("%H:%M:%S")
        if self.baseline is None:
            self.status.set(
                f"{now}  完整读取成功 {self.successful_cycles} 次；点击“记录基准”后开始比较"
            )
        else:
            self.status.set(
                f"{now}  完整读取成功 {self.successful_cycles} 次；"
                f"FLASH 参数变化 {flash_changes} 个，全部变化 {all_changes} 个"
            )

    def record_baseline(self) -> None:
        if not self.current:
            messagebox.showwarning("尚无数据", "设备还没有完成一次完整读取。")
            return
        self.baseline = dict(self.current)
        self.last_logged = dict(self.current)
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.log_path = Path(__file__).resolve().parent / f"modbus_changes_{stamp}.csv"
        with self.log_path.open("w", newline="", encoding="utf-8-sig") as file:
            writer = csv.writer(file)
            writer.writerow(("时间", "寄存器", "区域", "记录值", "当前值"))
        self.status.set(f"基准已记录；变化日志：{self.log_path.name}")
        self.update_values(self.current)

    def clear_baseline(self) -> None:
        self.baseline = None
        self.last_logged.clear()
        self.log_path = None
        if self.current:
            self.update_values(self.current)

    def log_change(self, address: int, baseline_value: int, value: int) -> None:
        if self.log_path is None or self.last_logged.get(address) == value:
            return
        self.last_logged[address] = value
        with self.log_path.open("a", newline="", encoding="utf-8-sig") as file:
            csv.writer(file).writerow(
                (
                    datetime.now().isoformat(timespec="seconds"),
                    40001 + address,
                    area_name(address),
                    baseline_value,
                    value,
                )
            )

    def close(self) -> None:
        self.stop_event.set()
        self.root.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description="持续读取 UMF 全部有效 Modbus 寄存器并比较上下电变化")
    parser.add_argument("--port", default="COM8", help="串口，默认 COM8")
    parser.add_argument("--baud", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument("--slave", type=int, default=2, help="Modbus 从站地址，默认 2")
    parser.add_argument("--interval", type=float, default=1.0, help="轮询周期（秒），默认 1")
    args = parser.parse_args()

    root = tk.Tk()
    if serial is None:
        root.withdraw()
        messagebox.showerror("缺少依赖", "请先执行：python -m pip install pyserial")
        root.destroy()
        return

    RegisterMonitor(root, args.port, args.baud, args.slave, max(0.2, args.interval))
    root.mainloop()


if __name__ == "__main__":
    main()
