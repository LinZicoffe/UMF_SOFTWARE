#!/usr/bin/env python
"""
Modbus RTU 从站抗压测试脚本

在指定波特率下，以不同帧间隔、不同寄存器地址、不同读取长度
对从站设备持续发送 FC03/FC04 读请求，统计响应成功率与延迟分布。

仅执行读操作，不写入任何寄存器，不影响设备运行状态。

依赖: pip install pymodbus pyserial
"""

from __future__ import annotations

import argparse
import json
import random
import signal
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
if sys.stderr and hasattr(sys.stderr, "reconfigure"):
    try:
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

try:
    from pymodbus.client import ModbusSerialClient
except ImportError:
    print("pymodbus 未安装，请运行: pip install pymodbus pyserial")
    sys.exit(1)

# ── 常量 ─────────────────────────────────────────────────────

MAX_REG_ADDR = 123  # 设备寄存器最大地址 (40123 对应地址 123)

DEFAULT_ADDRESSES = [0, 7, 20, 40, 60, 90]
DEFAULT_LENGTHS   = [1, 2, 4, 8, 10]
DEFAULT_DELAYS_MS = [0, 5, 10, 20, 50, 100]


# ── 数据结构 ─────────────────────────────────────────────────

@dataclass
class SingleResult:
    ok: bool
    latency_ms: float
    error_type: str | None = None  # None / timeout / modbus_error / crc_error / exception


@dataclass
class GroupResult:
    address: int
    count: int
    delay_ms: int
    func_code: int
    total: int = 0
    success: int = 0
    timeout: int = 0
    error: int = 0
    crc_error: int = 0
    latencies: list[float] = field(default_factory=list)

    def add(self, r: SingleResult):
        self.total += 1
        if r.ok:
            self.success += 1
            self.latencies.append(r.latency_ms)
        elif r.error_type == "timeout":
            self.timeout += 1
        elif r.error_type in ("crc_error", "exception"):
            self.crc_error += 1
        else:
            self.error += 1

    @property
    def success_rate(self) -> float:
        return self.success / self.total * 100 if self.total else 0.0

    @property
    def avg_latency(self) -> float:
        return sum(self.latencies) / len(self.latencies) if self.latencies else 0.0

    @property
    def max_latency(self) -> float:
        return max(self.latencies) if self.latencies else 0.0

    def to_dict(self) -> dict:
        return {
            "address": self.address,
            "count": self.count,
            "delay_ms": self.delay_ms,
            "func_code": self.func_code,
            "total": self.total,
            "success": self.success,
            "timeout": self.timeout,
            "error": self.error,
            "crc_error": self.crc_error,
            "success_rate_pct": round(self.success_rate, 1),
            "avg_latency_ms": round(self.avg_latency, 1),
            "max_latency_ms": round(self.max_latency, 1),
        }


# ── 全局中断 ─────────────────────────────────────────────────

_interrupted = False


def _signal_handler(sig, frame):
    global _interrupted
    _interrupted = True
    print("\n\n>> Ctrl+C: 完成当前组后退出...\n")


# ── 核心函数 ─────────────────────────────────────────────────

def send_read(client: ModbusSerialClient, slave: int,
              address: int, count: int, func_code: int) -> SingleResult:
    t0 = time.perf_counter()
    try:
        if func_code == 3:
            rr = client.read_holding_registers(address=address, count=count, device_id=slave)
        else:
            rr = client.read_input_registers(address=address, count=count, device_id=slave)
        latency = (time.perf_counter() - t0) * 1000.0
        if rr.isError():
            return SingleResult(ok=False, latency_ms=latency, error_type="modbus_error")
        return SingleResult(ok=True, latency_ms=latency)
    except Exception as e:
        latency = (time.perf_counter() - t0) * 1000.0
        s = str(e).lower()
        if "timeout" in s:
            et = "timeout"
        elif "crc" in s:
            et = "crc_error"
        else:
            et = "exception"
        return SingleResult(ok=False, latency_ms=latency, error_type=et)


def run_group(client: ModbusSerialClient, slave: int,
              address: int, count: int, func_code: int,
              delay_ms: int, rounds: int) -> GroupResult:
    result = GroupResult(address=address, count=count,
                         delay_ms=delay_ms, func_code=func_code)
    delay_s = delay_ms / 1000.0
    for _ in range(rounds):
        if _interrupted:
            break
        r = send_read(client, slave, address, count, func_code)
        result.add(r)
        if delay_s > 0 and not _interrupted:
            time.sleep(delay_s)
    return result


def valid_combinations(addresses: list[int], lengths: list[int]) -> list[tuple[int, int]]:
    combos = []
    for addr in addresses:
        for cnt in lengths:
            if addr + cnt <= MAX_REG_ADDR + 1:
                combos.append((addr, cnt))
    return combos


# ── Phase 1: 基线验证 ───────────────────────────────────────

def phase1_baseline(client, slave, combos) -> bool:
    print("\n" + "=" * 60)
    print("Phase 1: 基线验证  (每组 1 次 FC03 读取)")
    print("=" * 60)

    passed = failed = 0
    for addr, cnt in combos:
        r = send_read(client, slave, addr, cnt, 3)
        tag = "OK" if r.ok else f"FAIL({r.error_type})"
        print(f"  addr={addr:>3} len={cnt:>2}  ->  {tag:>16}  {r.latency_ms:>6.1f}ms")
        if r.ok:
            passed += 1
        else:
            failed += 1

    print(f"\n  结果: {passed} 通过, {failed} 失败")
    return failed < len(combos)


# ── Phase 2: 固定间隔压力 ──────────────────────────────────

def phase2_fixed_interval(client, slave, combos, delays_ms, rounds) -> list[GroupResult]:
    total_groups = len(combos) * len(delays_ms)
    print("\n" + "=" * 60)
    print(f"Phase 2: 固定间隔压力  ({total_groups} 组 x {rounds} 轮)")
    print("=" * 60)

    results = []
    done = 0

    for delay_ms in delays_ms:
        if _interrupted:
            break
        print(f"\n  -- 帧间隔 {delay_ms}ms --")
        for addr, cnt in combos:
            if _interrupted:
                break
            done += 1
            r = run_group(client, slave, addr, cnt, 3, delay_ms, rounds)
            results.append(r)

            rate = f"{r.success_rate:.0f}%"
            avg = f"{r.avg_latency:.1f}" if r.latencies else "N/A"
            mx  = f"{r.max_latency:.1f}" if r.latencies else "N/A"
            flag = "" if r.success_rate >= 99.9 else (" !!" if r.success_rate >= 90 else " FAIL")
            print(f"  [{done:>{len(str(total_groups))}}/{total_groups}] "
                  f"addr={addr:>3} len={cnt:>2} "
                  f"{r.success}/{r.total} {rate:>4} "
                  f"avg={avg:>7}ms max={mx:>7}ms{flag}")

    return results


# ── Phase 3: 极限压力 ───────────────────────────────────────

def phase3_extreme(client, slave, combos, rounds) -> list[GroupResult]:
    print("\n" + "=" * 60)
    print(f"Phase 3: 极限压力  (0ms 间隔, 每组 {rounds} 轮)")
    print("=" * 60)

    results = []
    for addr, cnt in combos:
        if _interrupted:
            break
        r = run_group(client, slave, addr, cnt, 3, 0, rounds)
        results.append(r)

        rate = f"{r.success_rate:.0f}%"
        flag = "" if r.success_rate >= 99.9 else (" !!" if r.success_rate >= 90 else " FAIL")
        print(f"  addr={addr:>3} len={cnt:>2} "
              f"{r.success}/{r.total} {rate:>4} "
              f"avg={r.avg_latency:.1f}ms max={r.max_latency:.1f}ms{flag}")

    return results


# ── Phase 4: 混合随机压力 ──────────────────────────────────

def phase4_mixed(client, slave, combos, total_requests, delay_ms) -> list[GroupResult]:
    print("\n" + "=" * 60)
    print(f"Phase 4: 混合随机压力  ({total_requests} 次, 间隔 {delay_ms}ms)")
    print("=" * 60)

    merged = GroupResult(address=-1, count=-1, delay_ms=delay_ms, func_code=3)
    delay_s = delay_ms / 1000.0

    for i in range(total_requests):
        if _interrupted:
            break
        addr, cnt = random.choice(combos)
        r = send_read(client, slave, addr, cnt, 3)
        merged.add(r)
        if delay_s > 0 and not _interrupted:
            time.sleep(delay_s)

        if (i + 1) % 50 == 0 or i == total_requests - 1:
            print(f"  [{i+1}/{total_requests}] "
                  f"OK={merged.success} TO={merged.timeout} "
                  f"ERR={merged.error} CRC={merged.crc_error} "
                  f"rate={merged.success_rate:.1f}%")

    return [merged]


# ── 报告 ─────────────────────────────────────────────────────

def print_summary(all_results: list[GroupResult]):
    print("\n" + "=" * 60)
    print("汇总报告")
    print("=" * 60)

    total_req     = sum(r.total for r in all_results)
    total_ok      = sum(r.success for r in all_results)
    total_timeout = sum(r.timeout for r in all_results)
    total_error   = sum(r.error for r in all_results)
    total_crc     = sum(r.crc_error for r in all_results)

    print(f"\n  总请求:  {total_req}")
    if total_req:
        print(f"  成功:    {total_ok} ({total_ok/total_req*100:.1f}%)")
    print(f"  超时:    {total_timeout}")
    print(f"  异常:    {total_error}")
    print(f"  CRC错:   {total_crc}")

    # 最稳定 / 最不稳定
    valid = [r for r in all_results if r.total >= 5 and r.address >= 0]
    if valid:
        best  = max(valid, key=lambda r: r.success_rate)
        worst = min(valid, key=lambda r: r.success_rate)
        print(f"\n  最稳定:   addr={best.address} len={best.count} "
              f"delay={best.delay_ms}ms -> {best.success_rate:.1f}%")
        print(f"  最不稳定: addr={worst.address} len={worst.count} "
              f"delay={worst.delay_ms}ms -> {worst.success_rate:.1f}%")

    # 按帧间隔汇总
    delays = sorted(set(r.delay_ms for r in all_results if r.address >= 0))
    if delays:
        print(f"\n  按帧间隔汇总:")
        print(f"  {'间隔':>6} | {'成功率':>7} | {'平均延迟':>8} | {'最大延迟':>8}")
        print(f"  {'------':>6}-+---------+----------+----------")
        for d in delays:
            grp = [r for r in all_results if r.delay_ms == d and r.address >= 0]
            if not grp:
                continue
            g_total = sum(r.total for r in grp)
            g_ok    = sum(r.success for r in grp)
            g_lat   = [l for r in grp for l in r.latencies]
            avg_l = sum(g_lat) / len(g_lat) if g_lat else 0
            max_l = max(g_lat) if g_lat else 0
            rate  = g_ok / g_total * 100 if g_total else 0
            print(f"  {d:>5}ms | {rate:>6.1f}% | {avg_l:>7.1f}ms | {max_l:>7.1f}ms")

    # 按读取长度汇总
    lengths = sorted(set(r.count for r in all_results if r.address >= 0))
    if lengths:
        print(f"\n  按读取长度汇总:")
        print(f"  {'长度':>4} | {'成功率':>7} | {'平均延迟':>8} | {'最大延迟':>8}")
        print(f"  {'----':>4}-+---------+----------+----------")
        for c in lengths:
            grp = [r for r in all_results if r.count == c and r.address >= 0]
            if not grp:
                continue
            g_total = sum(r.total for r in grp)
            g_ok    = sum(r.success for r in grp)
            g_lat   = [l for r in grp for l in r.latencies]
            avg_l = sum(g_lat) / len(g_lat) if g_lat else 0
            max_l = max(g_lat) if g_lat else 0
            rate  = g_ok / g_total * 100 if g_total else 0
            print(f"  {c:>4} | {rate:>6.1f}% | {avg_l:>7.1f}ms | {max_l:>7.1f}ms")


def save_json(all_results: list[GroupResult], output_path: str):
    data = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "results": [r.to_dict() for r in all_results],
    }
    p = Path(output_path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"\n结果已保存: {p.resolve()}")


# ── CLI ──────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Modbus RTU 从站抗压测试  (只读, 不写入寄存器)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
示例:
  # 完整测试 (默认 115200, 从站 2)
  python modbus_stress_test.py --port COM3 --slave 2

  # 快速模式
  python modbus_stress_test.py --port COM3 --slave 2 --quick

  # 仅运行 Phase 2, 自定义帧间隔
  python modbus_stress_test.py --port COM3 --slave 2 --skip-phase 1 --skip-phase 3 --skip-phase 4 --delays 0,5,10

  # 自定义地址和长度
  python modbus_stress_test.py --port COM3 --slave 2 --addresses 0,20,60 --lengths 1,4,10
""",
    )

    # 连接
    conn = p.add_argument_group("连接参数")
    conn.add_argument("--port", required=True, help="串口 (如 COM3, /dev/ttyUSB0)")
    conn.add_argument("--baudrate", type=int, default=115200, help="波特率 (默认 115200)")
    conn.add_argument("--slave", type=int, default=2, help="从站地址 (默认 2)")
    conn.add_argument("--parity", choices=["N", "E", "O"], default="N", help="校验 (默认 N)")
    conn.add_argument("--stopbits", type=int, choices=[1, 2], default=1, help="停止位 (默认 1)")
    conn.add_argument("--timeout", type=float, default=0.5, help="单次请求超时, 秒 (默认 0.5)")

    # 测试参数
    test = p.add_argument_group("测试参数")
    test.add_argument("--rounds", type=int, default=100,
                      help="Phase 2 每组重复次数 (默认 100)")
    test.add_argument("--delays", default="0,5,10,20,50,100",
                      help="Phase 2 帧间隔 ms, 逗号分隔 (默认 0,5,10,20,50,100)")
    test.add_argument("--addresses", default="0,7,20,40,60,90",
                      help="测试寄存器地址, 逗号分隔 (默认 0,7,20,40,60,90)")
    test.add_argument("--lengths", default="1,2,4,8,10",
                      help="测试读取长度, 逗号分隔 (默认 1,2,4,8,10)")
    test.add_argument("--extreme-rounds", type=int, default=500,
                      help="Phase 3 极限测试轮次 (默认 500)")
    test.add_argument("--mixed-count", type=int, default=500,
                      help="Phase 4 混合测试总次数 (默认 500)")
    test.add_argument("--mixed-delay", type=int, default=0,
                      help="Phase 4 混合测试帧间隔 ms (默认 0)")

    # 控制
    ctrl = p.add_argument_group("控制")
    ctrl.add_argument("--quick", action="store_true",
                      help="快速模式 (减少轮次和组合)")
    ctrl.add_argument("--skip-phase", type=int, action="append", default=[],
                      help="跳过指定 Phase (如 --skip-phase 1 --skip-phase 3)")
    ctrl.add_argument("--output", default="modbus_stress_result.json",
                      help="JSON 结果文件 (默认 modbus_stress_result.json)")

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    signal.signal(signal.SIGINT, _signal_handler)

    # 解析列表参数
    delays    = [int(x) for x in args.delays.split(",")]
    addresses = [int(x) for x in args.addresses.split(",")]
    lengths   = [int(x) for x in args.lengths.split(",")]

    # 快速模式调整
    if args.quick:
        args.rounds         = 20
        args.extreme_rounds = 100
        args.mixed_count    = 100
        addresses = addresses[:3]
        lengths   = lengths[:3]
        delays    = delays[:3]

    combos = valid_combinations(addresses, lengths)
    if not combos:
        print("没有有效的 (地址, 长度) 组合")
        return 1

    # 打印配置
    print("=" * 60)
    print("Modbus RTU 从站抗压测试")
    print("=" * 60)
    print(f"  串口:       {args.port}")
    print(f"  波特率:     {args.baudrate}")
    print(f"  从站地址:   {args.slave}")
    print(f"  校验/停止:  {args.parity}/{args.stopbits}")
    print(f"  请求超时:   {args.timeout}s")
    print(f"  测试地址:   {addresses}")
    print(f"  测试长度:   {lengths}")
    print(f"  帧间隔:     {delays} ms")
    print(f"  有效组合:   {len(combos)}")
    print(f"  Phase 2 轮: {args.rounds}")
    print(f"  Phase 3 轮: {args.extreme_rounds}")
    print(f"  Phase 4 次: {args.mixed_count}")
    if args.quick:
        print(f"  模式:       快速")
    skip = args.skip_phase
    if skip:
        print(f"  跳过:       Phase {', '.join(str(p) for p in sorted(skip))}")

    # 连接
    client = ModbusSerialClient(
        port=args.port,
        baudrate=args.baudrate,
        parity=args.parity,
        stopbits=args.stopbits,
        bytesize=8,
        timeout=args.timeout,
    )
    if not client.connect():
        print(f"\n连接失败: {args.port}")
        return 1
    print(f"\n已连接: {args.port} @ {args.baudrate}")

    all_results: list[GroupResult] = []

    try:
        # Phase 1
        if 1 not in skip:
            ok = phase1_baseline(client, args.slave, combos)
            if not ok:
                print("\n!! 基线全部失败，请检查设备连接。仍尝试后续测试...")
        else:
            print("\n>> 跳过 Phase 1")

        # Phase 2
        if 2 not in skip and not _interrupted:
            all_results.extend(
                phase2_fixed_interval(client, args.slave, combos, delays, args.rounds)
            )
        else:
            if 2 in skip:
                print("\n>> 跳过 Phase 2")

        # Phase 3
        if 3 not in skip and not _interrupted:
            all_results.extend(
                phase3_extreme(client, args.slave, combos, args.extreme_rounds)
            )
        else:
            if 3 in skip:
                print("\n>> 跳过 Phase 3")

        # Phase 4
        if 4 not in skip and not _interrupted:
            all_results.extend(
                phase4_mixed(client, args.slave, combos,
                             args.mixed_count, args.mixed_delay)
            )
        else:
            if 4 in skip:
                print("\n>> 跳过 Phase 4")

    finally:
        client.close()
        print("\n连接已关闭")

    # 报告
    if all_results:
        print_summary(all_results)
        save_json(all_results, args.output)
    else:
        print("\n未收集到测试结果")

    return 0


if __name__ == "__main__":
    sys.exit(main())
