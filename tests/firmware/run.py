"""Cross-compile unchanged production sources and execute ARM instructions.

Run from the repository root. Each case gets a fresh Cortex-M3 and fresh RAM.
Only test doubles may replace hardware boundaries; no Python algorithm oracle
is used as an implementation of the function under test.
"""
from __future__ import annotations

import argparse
from collections import deque
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
FLASH = (0x08000000, 0x10000)
RAM = (0x20000000, 0x5000)
STACK_TOP = 0x20004C00
STACK_BOTTOM = STACK_TOP - 0x1000
RETURN = 0x0800FFF0
PRODUCTION = ["BSP/ftoa.c", "BSP/key.c", "BSP/Src/cal_table.c",
              "BSP/bsp_usart.c", "BSP/mystring.c"]
GROUPS = ("all", "regression", "smoke", "runner", "keys", "calibration",
          "sensor", "protocol", "known-defects")
PROBES = {
    "test_runner_reject_invalid_read": ("probe_invalid_read", "UC_ERR_READ_UNMAPPED"),
    "test_runner_reject_invalid_write": ("probe_invalid_write", "UC_ERR_WRITE_UNMAPPED"),
    "test_runner_reject_flash_write": ("probe_flash_write", "UC_ERR_WRITE_PROT"),
    "test_runner_reject_invalid_instruction": ("probe_invalid_instruction", "CPU error"),
    "test_runner_reject_instruction_limit": ("probe_loop", "did not return normally"),
    "test_runner_reject_time_limit": ("probe_loop", "did not return normally"),
    "test_runner_reject_bad_stack": ("probe_bad_stack", "stack pointer not restored"),
}


def required_target(name):
    """A passing assertion is insufficient unless the real function was reached."""
    if name == "test_smoke_itoa":
        return "itoa_local"
    if name == "test_smoke_float_format":
        return "ftoa"
    if name.startswith("test_keys_"):
        return "key_scan_10ms"
    if name.startswith("test_calibration_"):
        return "cal_correct_flow"
    if name == "test_protocol_crc_reference_vector":
        return "getCRC16"
    if name.startswith(("test_protocol_", "test_known_")):
        return "Modbus_Function_1"
    if name == "test_sensor_unit_conversion":
        return "convert_flow_rate_from_lph"
    if name.startswith(("test_sensor_poll_", "test_sensor_timeout_", "test_sensor_interval_")):
        return "Uart1_Send_Function"
    if name.startswith("test_sensor_"):
        return "Uart1_Receive_Function"
    if name.startswith("test_runner_"):
        return None
    raise RegressionError(f"test lacks a production target declaration: {name}")


class RegressionError(Exception):
    pass


def command_run(command, cwd, timeout=120, temp_dir=None):
    child_env = os.environ.copy()
    if temp_dir is not None:
        child_env.update({name: str(temp_dir) for name in ("TMP", "TEMP", "TMPDIR")})
    try:
        result = subprocess.run(command, cwd=cwd, capture_output=True,
                                text=True, errors="replace", timeout=timeout, env=child_env)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise RegressionError(f"cannot run {command[0]}: {exc}") from exc
    if result.returncode:
        raise RegressionError(f"command exited {result.returncode}: {command}\n"
                              f"{result.stdout}\n{result.stderr}")
    return result


def build(args, out):
    cc = shutil.which(args.cc)
    if not cc:
        raise RegressionError(f"compiler not found: {args.cc}; pass --cc or ARM_CC")
    version = command_run([cc, "--version"], ROOT).stdout.splitlines()[0]
    machine = command_run([cc, "-dumpmachine"], ROOT).stdout.strip()
    if machine != "arm-none-eabi":
        raise RegressionError(f"expected arm-none-eabi compiler, got {machine}")
    sources = [ROOT / name for name in PRODUCTION]
    sources += [HERE / name for name in ("smoke.c", "doubles.c", "cases.c")]
    includes = ["Core/Inc", "BSP", "Drivers/STM32F1xx_HAL_Driver/Inc",
                "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy",
                "Drivers/CMSIS/Device/ST/STM32F1xx/Include", "Drivers/CMSIS/Include"]
    command = [cc, "-std=c99", "-mcpu=cortex-m3", "-mthumb", "-mlittle-endian",
               "-mfloat-abi=soft", "-Os", "-g3", "-ffunction-sections",
               "-fdata-sections", "-fno-builtin", "-Wall", "-Wextra",
               "-DUSE_HAL_DRIVER", "-DSTM32F103xB", "-I" + str(HERE)]
    command += ["-I" + str(ROOT / path) for path in includes]
    command += [str(path) for path in sources]
    elf = out / "regression.elf"
    command += ["-nostartfiles", "--specs=nosys.specs", "-T", str(HERE / "linker.ld"),
                "-Wl,--gc-sections", "-Wl,-Map=" + str(out / "regression.map"),
                "-o", str(elf), "-lm"]
    (out / "build-command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
    temp_dir = out / "tmp"
    temp_dir.mkdir(exist_ok=True)
    try:
        result = command_run(command, ROOT, temp_dir=temp_dir)
    except RegressionError as exc:
        (out / "build.log").write_text(str(exc), encoding="utf-8")
        raise
    (out / "build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    # Keep real target disassembly and ABI attributes, rather than just claiming ARM.
    suffix = ".exe" if cc.lower().endswith(".exe") else ""
    bindir = Path(cc).parent
    for name, flags, filename in [("objdump", ["-d", "-S"], "disassembly.txt"),
                                   ("readelf", ["-h", "-A", "-l"], "elf-info.txt")]:
        tool = bindir / ("arm-none-eabi-" + name + suffix)
        info = command_run([str(tool), *flags, str(elf)], ROOT)
        (out / filename).write_text(info.stdout, encoding="utf-8")
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sources}
    return elf, {"compiler": version, "machine": machine, "sources_sha256": hashes,
                 "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}


class Image:
    def __init__(self, path):
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            if (elf.elfclass != 32 or not elf.little_endian or
                    elf["e_machine"] != "EM_ARM" or elf["e_type"] != "ET_EXEC"):
                raise RegressionError("ELF must be ARM32 little-endian ET_EXEC")
            self.segments = []
            for segment in elf.iter_segments():
                if segment["p_type"] != "PT_LOAD" or not segment["p_memsz"]:
                    continue
                addr, memsz = segment["p_vaddr"], segment["p_memsz"]
                if segment["p_filesz"] > memsz or not any(
                        base <= addr and addr + memsz <= base + size
                        for base, size in (FLASH, RAM)):
                    raise RegressionError(f"invalid PT_LOAD at 0x{addr:08x}")
                self.segments.append((addr, segment.data(), memsz))
            symtab = elf.get_section_by_name(".symtab")
            if symtab is None:
                raise RegressionError("ELF has no symbol table")
            self.symbols = {s.name: s["st_value"] for s in symtab.iter_symbols()
                            if s["st_shndx"] != "SHN_UNDEF"}
            self.sizes = {s.name: s["st_size"] for s in symtab.iter_symbols()}
            self.functions = {s["st_value"] & ~1: s.name for s in symtab.iter_symbols()
                              if s["st_info"]["type"] == "STT_FUNC"}
            # Link provenance comes from the compiler's DWARF compilation units.
            self.origins = {}
            for unit in elf.get_dwarf_info().iter_CUs():
                top = unit.get_top_DIE()
                origin = top.attributes["DW_AT_name"].value.decode("utf-8", errors="replace")
                for die in unit.iter_DIEs():
                    if die.tag != "DW_TAG_subprogram" or "DW_AT_low_pc" not in die.attributes:
                        continue
                    if "DW_AT_name" in die.attributes:
                        self.origins[die.attributes["DW_AT_name"].value.decode()] = origin
        if self.symbols["__ram_used_end"] >= STACK_BOTTOM:
            raise RegressionError("data overlaps reserved stack")
        for target in ("itoa_local", "ftoa", "key_scan_10ms", "cal_correct_flow",
                       "Modbus_Function_1", "getCRC16", "Uart1_Receive_Function",
                       "Uart1_Send_Function", "convert_flow_rate_from_lph"):
            origin = Path(self.origins.get(target, "")).resolve()
            if origin not in [ROOT / p for p in PRODUCTION]:
                raise RegressionError(f"production symbol has unexpected origin: {target}: {origin}")

    def execute(self, name, limit, timeout_ms):
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_LITTLE_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        uc.mem_map(*FLASH, UC_PROT_READ | UC_PROT_EXEC)
        uc.mem_map(*RAM, UC_PROT_READ | UC_PROT_WRITE)
        # Only IFCR writes used by the real TX callback are admitted here.
        # This is plain memory, NOT an automatic DMA/peripheral model.
        uc.mem_map(0x40020000, 0x1000, UC_PROT_READ | UC_PROT_WRITE)
        for addr, data, memsz in self.segments:
            uc.mem_write(addr, data + bytes(memsz - len(data)))
        # A sentinel in executable flash; never use the peripheral region as code.
        uc.mem_write(RETURN, b"\x00\xbe")
        uc.reg_write(UC_ARM_REG_SP, STACK_TOP)
        uc.reg_write(UC_ARM_REG_LR, RETURN | 1)
        uc.reg_write(UC_ARM_REG_XPSR, 1 << 24)
        visited, trace = set(), deque(maxlen=16)
        dma_writes, boundary_failures = [], []

        def instruction(cpu, addr, size, _):
            trace.append(addr)
            if addr in self.functions:
                visited.add(self.functions[addr])

        uc.hook_add(UC_HOOK_CODE, instruction)

        def peripheral(cpu, access, addr, size, value, _):
            if access == UC_MEM_WRITE and addr == 0x40020004 and size == 4:
                dma_writes.append({"address": hex(addr), "value": hex(value)})
            else:
                boundary_failures.append(f"unmodeled peripheral access at 0x{addr:08x}")
                cpu.emu_stop()

        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, peripheral,
                    begin=0x40020000, end=0x40020fff)
        failure = None
        try:
            uc.emu_start(self.symbols[name] | 1, RETURN,
                         timeout=timeout_ms * 1000, count=limit)
            if uc.reg_read(UC_ARM_REG_PC) != RETURN:
                failure = "did not return normally (instruction/time limit or wrong return)"
            elif uc.reg_read(UC_ARM_REG_SP) != STACK_TOP:
                failure = "stack pointer not restored on return"
        except UcError as exc:
            failure = f"CPU error: {exc}"
        if boundary_failures:
            failure = boundary_failures[0]
        if "sim_boundary_error" in self.symbols:
            code = struct.unpack("<I", uc.mem_read(self.symbols["sim_boundary_error"], 4))[0]
            if code:
                failure = f"test double rejected unexpected boundary operation: {code}"
        line = uc.reg_read(UC_ARM_REG_R0)
        if not failure and line:
            pointer = struct.unpack("<I", uc.mem_read(self.symbols["sim_failure"], 4))[0]
            detail = bytearray()
            if pointer:
                for index in range(512):
                    byte = uc.mem_read(pointer + index, 1)
                    if byte == b"\0":
                        break
                    detail.extend(byte)
            failure = f"C assertion line {line}: {detail.decode('utf-8', errors='replace')}"
        target = required_target(name) if name.startswith("test_") else None
        if target and target not in visited:
            failure = f"real production function was not executed: {target}"
        events = []
        if "sim_event_count" in self.symbols:
            count = struct.unpack("<I", uc.mem_read(self.symbols["sim_event_count"], 4))[0]
            # doubles.h: six uint32_t fields + UART_TX_LEN bytes, aligned by C.
            stride = self.sizes["sim_events"] // 32
            for index in range(min(count, 32)):
                data = uc.mem_read(self.symbols["sim_events"] + index * stride, stride)
                kind, at_ms, arg0, arg1, length, status = struct.unpack_from("<6I", data)
                events.append({"kind": kind, "at_ms": at_ms, "arg0": hex(arg0),
                               "arg1": hex(arg1), "length": length, "result": status,
                               "data_hex": bytes(data[24:24 + length]).hex(" ")})
        return {"name": name, "status": "FAIL" if failure else "PASS",
                "reason": failure, "pc": hex(uc.reg_read(UC_ARM_REG_PC)),
                "trace": [hex(addr) for addr in trace], "visited": sorted(visited),
                "production_target": target, "events": events, "dma_ifcr_writes": dma_writes}

    def check_probe(self, name, limit, timeout_ms):
        probe, expected = PROBES[name]
        if name.endswith("instruction_limit"):
            limit, timeout_ms = 200, 2000
        elif name.endswith("time_limit"):
            limit, timeout_ms = 1000000000, 1
        result = self.execute(probe, limit, timeout_ms)
        rejected = result["status"] == "FAIL" and expected in result["reason"]
        result.update(name=name, observed_rejection=result["reason"],
                      status="PASS" if rejected else "FAIL",
                      reason=None if rejected else f"probe was not rejected as {expected}")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("ARM_CC", "arm-none-eabi-gcc"))
    parser.add_argument("--group", action="append", choices=GROUPS)
    parser.add_argument("--case", action="append", help="substring of test function name")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--max-instructions", type=int, default=1000000)
    parser.add_argument("--timeout-ms", type=int, default=2000)
    parser.add_argument("--output", type=Path, default=HERE / "build" / "latest")
    args = parser.parse_args()
    out = args.output.resolve()
    report = {"timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "command": sys.argv, "python": platform.python_version(),
              "exit_code": 2, "results": []}
    try:
        out.mkdir(parents=True, exist_ok=True)
        if args.max_instructions <= 0 or args.timeout_ms <= 0:
            raise RegressionError("instruction and time limits must be positive")
        global ELFFile, Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_LITTLE_ENDIAN
        global UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC, UC_HOOK_CODE
        global UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_WRITE
        global UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_XPSR, UC_ARM_REG_PC, UC_ARM_REG_R0
        global UC_CPU_ARM_CORTEX_M3
        try:
            from elftools.elf.elffile import ELFFile
            import unicorn
            from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB,
                                 UC_MODE_LITTLE_ENDIAN, UC_PROT_READ, UC_PROT_WRITE,
                                 UC_PROT_EXEC, UC_HOOK_CODE, UC_HOOK_MEM_READ,
                                 UC_HOOK_MEM_WRITE, UC_MEM_WRITE)
            from unicorn.arm_const import (UC_ARM_REG_SP, UC_ARM_REG_LR,
                UC_ARM_REG_XPSR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_CPU_ARM_CORTEX_M3)
        except (ImportError, OSError) as exc:
            raise RegressionError(f"dependencies unavailable: {exc}; install requirements.txt in venv") from exc
        report["unicorn"] = unicorn.__version__
        report["unicorn_native"] = unicorn.uc_version()
        import elftools
        report["pyelftools"] = elftools.__version__
        report["git_head"] = command_run(["git", "rev-parse", "HEAD"], ROOT).stdout.strip()
        elf, metadata = build(args, out)
        report.update(metadata)
        image = Image(elf)
        groups = args.group or ["all"]
        report["production_symbol_origins"] = image.origins
        names = sorted([n for n in image.symbols if n.startswith("test_")] + list(PROBES))
        selected = [n for n in names if any(g == "all" or
                    (g == "regression" and not n.startswith("test_known_")) or
                    n.startswith("test_" + ("known" if g == "known-defects" else g) + "_")
                    for g in groups)]
        if args.case:
            selected = [n for n in selected if any(part in n for part in args.case)]
        if not selected:
            raise RegressionError("no matching tests")
        report["not_selected"] = sorted(set(names) - set(selected))
        if args.list:
            print("\n".join(selected))
            report["exit_code"] = 0
            return 0
        for name in selected:
            if name in PROBES:
                result = image.check_probe(name, args.max_instructions, args.timeout_ms)
            else:
                result = image.execute(name, args.max_instructions, args.timeout_ms)
            report["results"].append(result)
            print(f"{result['status']} {name}" + (f": {result['reason']}" if result["reason"] else ""))
        failed = sum(r["status"] == "FAIL" for r in report["results"])
        report["summary"] = {"pass": len(selected) - failed, "fail": failed}
        report["exit_code"] = 1 if failed else 0
        print(f"SUMMARY PASS={len(selected) - failed} FAIL={failed}")
        return report["exit_code"]
    except (RegressionError, UcError if "UcError" in globals() else RuntimeError,
            OSError, KeyError, ValueError) as exc:
        report["error"] = str(exc)
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2
    finally:
        try:
            (out / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        except OSError as exc:
            print(f"ERROR: cannot save report: {exc}", file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
