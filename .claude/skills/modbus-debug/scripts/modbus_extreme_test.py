#!/usr/bin/env python
"""
Modbus RTU 极限测试脚本

  1. 异步泛洪 — 不等响应连续发送 N 帧, 探测设备真实崩溃边界
  2. 边界地址 — 最大地址、越界地址、异常长度, 测试设备容错能力

通过原始串口操作绕过 pymodbus 的同步等待机制, 实现真正的异步泛洪。

依赖: pip install pyserial pymodbus
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

try:
    import serial
except ImportError:
    print("pyserial 未安装, 请运行: pip install pyserial")
    sys.exit(1)

try:
    from pymodbus.client import ModbusSerialClient
except ImportError:
    print("pymodbus 未安装, 请运行: pip install pymodbus")
    sys.exit(1)


# ── CRC16 Modbus ─────────────────────────────────────────────

def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_fc03(slave: int, addr: int, count: int) -> bytes:
    body = bytes([slave, 0x03,
                  (addr >> 8) & 0xFF, addr & 0xFF,
                  (count >> 8) & 0xFF, count & 0xFF])
    return body + struct.pack("<H", crc16(body))


# ── 帧解析 ───────────────────────────────────────────────────

def parse_responses(data: bytes, slave: int) -> list[dict]:
    """从字节流中解析所有完整 Modbus 响应帧"""
    results = []
    i = 0
    while i < len(data):
        if data[i] != slave:
            i += 1
            continue
        if i + 2 >= len(data):
            break
        func = data[i + 1]

        # 异常响应: slave + 0x8X + exc_code + CRC = 5 bytes
        if func & 0x80:
            end = i + 5
            if end > len(data):
                break
            frame = data[i:end]
            if crc16(frame[:-2]) == struct.unpack("<H", frame[-2:])[0]:
                results.append({"type": "exception", "func": func, "exc": data[i + 2]})
                i = end
                continue
            i += 1
            continue

        # 正常响应: slave + func + byte_count + N*data + CRC
        if func in (0x03, 0x04):
            if i + 3 >= len(data):
                break
            bc = data[i + 2]
            end = i + 3 + bc + 2
            if end > len(data):
                break
            frame = data[i:end]
            if crc16(frame[:-2]) == struct.unpack("<H", frame[-2:])[0]:
                results.append({"type": "ok", "func": func, "regs": bc // 2})
                i = end
                continue
            i += 1
            continue

        i += 1
    return results


def verify_alive_ser(ser, slave: int) -> bool:
    """在已打开的串口上验证设备是否存活"""
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    ser.write(build_fc03(slave, 0, 1))
    time.sleep(0.05)
    rx = ser.read(ser.in_waiting or 256)
    responses = parse_responses(rx, slave)
    return any(r["type"] == "ok" for r in responses)


def verify_alive_client(port: str, baudrate: int, slave: int) -> bool:
    """独立连接验证设备是否在线 (串口未占用时使用)"""
    client = ModbusSerialClient(
        port=port, baudrate=baudrate, parity="N",
        stopbits=1, bytesize=8, timeout=0.3,
    )
    if not client.connect():
        return False
    try:
        rr = client.read_holding_registers(address=0, count=1, device_id=slave)
        return not rr.isError()
    except Exception:
        return False
    finally:
        client.close()


# ── 异步泛洪 ─────────────────────────────────────────────────

@dataclass
class BurstResult:
    burst_size: int
    delay_us: int
    round_idx: int
    sent: int
    ok: int
    exceptions: int
    missing: int
    rx_bytes: int
    send_ms: float
    alive_after: bool

    @property
    def success_rate(self) -> float:
        return self.ok / self.sent * 100 if self.sent else 0

    @property
    def response_rate(self) -> float:
        return (self.ok + self.exceptions) / self.sent * 100 if self.sent else 0

    def to_dict(self) -> dict:
        return {
            "burst_size": self.burst_size,
            "delay_us": self.delay_us,
            "round": self.round_idx,
            "sent": self.sent,
            "ok": self.ok,
            "exceptions": self.exceptions,
            "missing": self.missing,
            "success_rate_pct": round(self.success_rate, 1),
            "response_rate_pct": round(self.response_rate, 1),
            "rx_bytes": self.rx_bytes,
            "send_ms": round(self.send_ms, 1),
            "alive_after": self.alive_after,
        }


def flood_test(port: str, baudrate: int, slave: int,
               burst_sizes: list[int], delays_us: list[int],
               rounds: int) -> list[BurstResult]:

    # 预构建请求帧 (交替地址, 模拟真实上位机)
    templates = [
        build_fc03(slave, 0, 2),    # 瞬时流量
        build_fc03(slave, 20, 2),   # DAC 参数
        build_fc03(slave, 40, 4),   # 累积流量
        build_fc03(slave, 60, 2),   # 扩展状态
    ]

    all_results: list[BurstResult] = []

    ser = serial.Serial(port, baudrate, timeout=0.1)
    time.sleep(0.1)

    try:
        for burst in burst_sizes:
            for delay_us in delays_us:
                print(f"\n  -- 泛洪 {burst} 帧, 帧间隔 {delay_us}us --")

                for r in range(rounds):
                    # 清空缓冲
                    ser.reset_input_buffer()
                    ser.reset_output_buffer()
                    time.sleep(0.05)

                    # 发送泛洪
                    t0 = time.perf_counter()
                    for i in range(burst):
                        ser.write(templates[i % len(templates)])
                        if delay_us > 0:
                            time.sleep(delay_us / 1_000_000)
                    send_ms = (time.perf_counter() - t0) * 1000

                    # 等待响应 (每帧约 10ms + 安全余量)
                    wait = burst * 0.015 + 0.2
                    time.sleep(wait)

                    # 读取全部响应
                    rx_bytes = ser.in_waiting
                    rx_data = ser.read(rx_bytes) if rx_bytes else b""

                    # 解析
                    responses = parse_responses(rx_data, slave)
                    ok_count = sum(1 for r in responses if r["type"] == "ok")
                    exc_count = sum(1 for r in responses if r["type"] == "exception")
                    missing = burst - ok_count - exc_count

                    # 验证设备是否存活 (复用同一串口)
                    time.sleep(0.1)
                    alive = verify_alive_ser(ser, slave)

                    result = BurstResult(
                        burst_size=burst, delay_us=delay_us,
                        round_idx=r + 1, sent=burst,
                        ok=ok_count, exceptions=exc_count,
                        missing=missing, rx_bytes=len(rx_data),
                        send_ms=send_ms, alive_after=alive,
                    )
                    all_results.append(result)

                    alive_tag = "" if alive else " !! 设备无响应"
                    print(f"  轮次 {r+1}/{rounds}: "
                          f"OK={ok_count} 异常={exc_count} 丢失={missing} "
                          f"成功率={result.success_rate:.0f}% "
                          f"发送={send_ms:.1f}ms "
                          f"接收={len(rx_data)}B{alive_tag}")

                    if not alive:
                        print("  !! 设备掉线, 等待恢复...")
                        time.sleep(2.0)
                        if not verify_alive_ser(ser, slave):
                            print("  !! 设备未恢复, 终止泛洪测试")
                            return all_results

                    # 轮间间隔
                    time.sleep(0.3)
    finally:
        ser.close()

    return all_results


# ── 边界地址测试 ─────────────────────────────────────────────

@dataclass
class BoundaryResult:
    address: int
    count: int
    description: str
    expected: str
    actual: str
    detail: str
    latency_ms: float
    ok: bool

    def to_dict(self) -> dict:
        return {
            "address": self.address, "count": self.count,
            "description": self.description,
            "expected": self.expected, "actual": self.actual,
            "detail": self.detail,
            "latency_ms": round(self.latency_ms, 1),
            "ok": self.ok,
        }


def boundary_test(port: str, baudrate: int, slave: int,
                  timeout: float) -> list[BoundaryResult]:

    # (地址, 长度, 描述, 预期)
    cases = [
        (0,    1,   "最小地址, 最小长度",              "ok"),
        (0,    2,   "瞬时流量 (float, 2 regs)",        "ok"),
        (0,    62,  "地址0读取62个寄存器",             "ok"),
        (0,    123, "地址0读取123个 (全覆盖)",          "ok"),
        (0,    124, "地址0读取124个 (超出1)",           "exception"),
        (0,    125, "地址0读取125个 (超出2)",           "exception"),
        (122,  1,   "倒数第二个地址 (122)",             "ok"),
        (122,  2,   "最后两个有效地址 (122+123)",       "ok"),
        (123,  1,   "最大有效地址 (123)",               "ok"),
        (123,  2,   "地址123读取2个 (越界)",            "exception"),
        (124,  1,   "第一个无效地址 (124)",             "exception"),
        (125,  1,   "无效地址+1 (125)",                 "exception"),
        (200,  1,   "远越界地址 (200)",                 "exception"),
        (1000, 1,   "极端越界地址 (1000)",              "exception"),
        (0,    0,   "读取长度为0",                      "exception"),
        (0xFFFF, 1, "地址0xFFFF",                       "exception"),
    ]

    client = ModbusSerialClient(
        port=port, baudrate=baudrate, parity="N",
        stopbits=1, bytesize=8, timeout=timeout,
    )
    if not client.connect():
        print(f"连接失败: {port}")
        return []

    results = []

    try:
        for addr, cnt, desc, expected in cases:
            t0 = time.perf_counter()
            try:
                rr = client.read_holding_registers(
                    address=addr, count=cnt, device_id=slave)
                lat = (time.perf_counter() - t0) * 1000

                if rr.isError():
                    actual = "exception"
                    detail = str(rr)
                else:
                    actual = "ok"
                    detail = f"{len(rr.registers)} regs"
            except Exception as e:
                lat = (time.perf_counter() - t0) * 1000
                actual = "timeout"
                detail = str(e)[:60]

            passed = (actual == expected)
            tag = "PASS" if passed else "WARN"
            print(f"  [{tag}] addr={addr:>5} len={cnt:>3}  "
                  f"{desc:<32} "
                  f"预期={expected:<10} 实际={actual:<10} "
                  f"{lat:.1f}ms")

            results.append(BoundaryResult(
                address=addr, count=cnt, description=desc,
                expected=expected, actual=actual, detail=detail,
                latency_ms=lat, ok=passed,
            ))
    finally:
        client.close()

    return results


# ── 报告 ─────────────────────────────────────────────────────

def print_flood_summary(results: list[BurstResult]):
    print("\n" + "=" * 60)
    print("异步泛洪测试汇总")
    print("=" * 60)

    if not results:
        return

    total_sent = sum(r.sent for r in results)
    total_ok = sum(r.ok for r in results)
    total_exc = sum(r.exceptions for r in results)
    total_missing = sum(r.missing for r in results)
    alive_count = sum(1 for r in results if r.alive_after)

    print(f"\n  总发送: {total_sent}")
    if total_sent:
        print(f"  正确响应: {total_ok} ({total_ok/total_sent*100:.1f}%)")
    print(f"  异常响应: {total_exc}")
    print(f"  无响应(丢失): {total_missing}")
    if total_sent:
        print(f"  设备响应率: {(total_ok+total_exc)/total_sent*100:.1f}%")
    print(f"  设备存活: {alive_count}/{len(results)} 轮")

    # 按泛洪量汇总
    print(f"\n  按泛洪帧数汇总:")
    print(f"  {'帧数':>4} | {'成功率':>7} | {'响应率':>7} | {'存活':>4}")
    print(f"  {'----':>4}-+---------+---------+------")
    for size in sorted(set(r.burst_size for r in results)):
        grp = [r for r in results if r.burst_size == size]
        g_sent = sum(r.sent for r in grp)
        g_ok = sum(r.ok for r in grp)
        g_exc = sum(r.exceptions for r in grp)
        g_alive = sum(1 for r in grp if r.alive_after)
        rate = g_ok / g_sent * 100 if g_sent else 0
        recovery = (g_ok + g_exc) / g_sent * 100 if g_sent else 0
        print(f"  {size:>4} | {rate:>6.1f}% | {recovery:>6.1f}% | {g_alive}/{len(grp)}")

    # 按帧间隔汇总
    print(f"\n  按帧间延迟汇总:")
    print(f"  {'延迟':>8} | {'成功率':>7} | {'响应率':>7} | {'存活':>4}")
    print(f"  {'--------':>8}-+---------+---------+------")
    for delay in sorted(set(r.delay_us for r in results)):
        grp = [r for r in results if r.delay_us == delay]
        g_sent = sum(r.sent for r in grp)
        g_ok = sum(r.ok for r in grp)
        g_exc = sum(r.exceptions for r in grp)
        g_alive = sum(1 for r in grp if r.alive_after)
        rate = g_ok / g_sent * 100 if g_sent else 0
        recovery = (g_ok + g_exc) / g_sent * 100 if g_sent else 0
        print(f"  {delay:>7}us | {rate:>6.1f}% | {recovery:>6.1f}% | {g_alive}/{len(grp)}")


def print_boundary_summary(results: list[BoundaryResult]):
    print("\n" + "=" * 60)
    print("边界地址测试汇总")
    print("=" * 60)

    passed = sum(1 for r in results if r.ok)
    failed = len(results) - passed
    print(f"\n  通过: {passed}/{len(results)}")
    if failed:
        print(f"  未达预期: {failed}")
        for r in results:
            if not r.ok:
                print(f"    addr={r.address} len={r.count}: "
                      f"预期={r.expected} 实际={r.actual} — {r.description}")


# ── CLI ──────────────────────────────────────────────────────

def build_parser():
    p = argparse.ArgumentParser(
        description="Modbus RTU 极限测试 (异步泛洪 + 边界地址)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
示例:
  # 运行全部极限测试
  py modbus_extreme_test.py --port COM7 --slave 2

  # 仅泛洪测试, 自定义参数
  py modbus_extreme_test.py --port COM7 --slave 2 --flood --burst-sizes 3,5,10,20,50

  # 仅边界测试
  py modbus_extreme_test.py --port COM7 --slave 2 --boundary
""",
    )
    conn = p.add_argument_group("连接")
    conn.add_argument("--port", required=True)
    conn.add_argument("--baudrate", type=int, default=115200)
    conn.add_argument("--slave", type=int, default=2)
    conn.add_argument("--timeout", type=float, default=0.5)

    flood = p.add_argument_group("异步泛洪")
    flood.add_argument("--flood", action="store_true")
    flood.add_argument("--burst-sizes", default="3,5,10,20,50",
                       help="泛洪帧数 (默认 3,5,10,20,50)")
    flood.add_argument("--flood-delays", default="0,500,1000",
                       help="帧间延迟 us (默认 0,500,1000)")
    flood.add_argument("--flood-rounds", type=int, default=3)

    boundary = p.add_argument_group("边界地址")
    boundary.add_argument("--boundary", action="store_true")

    p.add_argument("--output", default="modbus_extreme_result.json")
    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if not args.flood and not args.boundary:
        args.flood = True
        args.boundary = True

    print("=" * 60)
    print("Modbus RTU 极限测试")
    print("=" * 60)
    print(f"  串口: {args.port} @ {args.baudrate}")
    print(f"  从站: {args.slave}")

    all_data = {"timestamp": time.strftime("%Y-%m-%d %H:%M:%S"), "flood": [], "boundary": []}

    # ── 异步泛洪 ──
    if args.flood:
        burst_sizes = [int(x) for x in args.burst_sizes.split(",")]
        delays = [int(x) for x in args.flood_delays.split(",")]
        print(f"\n  泛洪帧数: {burst_sizes}")
        print(f"  帧间延迟: {delays} us")
        print(f"  每组轮次: {args.flood_rounds}")

        print("\n" + "=" * 60)
        print("测试 1: 异步泛洪")
        print("=" * 60)

        flood_results = flood_test(
            args.port, args.baudrate, args.slave,
            burst_sizes, delays, args.flood_rounds,
        )
        print_flood_summary(flood_results)
        all_data["flood"] = [r.to_dict() for r in flood_results]

    # ── 边界地址 ──
    if args.boundary:
        print("\n" + "=" * 60)
        print("测试 2: 边界地址 + 越界")
        print("=" * 60)

        boundary_results = boundary_test(
            args.port, args.baudrate, args.slave, args.timeout,
        )
        print_boundary_summary(boundary_results)
        all_data["boundary"] = [r.to_dict() for r in boundary_results]

    # 保存
    p = Path(args.output)
    p.write_text(json.dumps(all_data, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n结果已保存: {p.resolve()}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
