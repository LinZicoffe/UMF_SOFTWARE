#!/usr/bin/env python3
"""em_skill 工具路径配置管理 CLI。

子命令:
  set <tool> <path> [--global]  保存工具路径
  get <tool>                    查询工具路径
  list                          列出所有已配置的工具
  remove <tool> [--global]      删除配置项
  path                          显示配置文件路径
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(_REPO_ROOT / "shared"))

from tool_config import (
    get_tool_path,
    list_tools,
    remove_tool_path,
    set_tool_path,
    user_config_path,
    workspace_config_path,
)


def cmd_set(args: argparse.Namespace) -> int:
    tool_path = str(Path(args.path).resolve())
    cfg_path = set_tool_path(args.tool, tool_path, global_=args.global_flag)
    level = "全局" if args.global_flag else "工作区"
    print(f"✅ 已保存 {args.tool} = {tool_path} ({level}: {cfg_path})")
    return 0


def cmd_get(args: argparse.Namespace) -> int:
    path = get_tool_path(args.tool)
    if path:
        print(path)
        return 0
    print(f"❌ 未找到 {args.tool} 的配置")
    return 1


def cmd_list(_args: argparse.Namespace) -> int:
    tools = list_tools()
    if not tools:
        print("ℹ️ 暂无已配置的工具")
        return 0
    print("📋 已配置的工具：")
    for name, info in sorted(tools.items()):
        print(f"  {name} = {info['path']}  [{info['source']}]")
    return 0


def cmd_remove(args: argparse.Namespace) -> int:
    removed = remove_tool_path(args.tool, global_=args.global_flag)
    if removed:
        level = "全局" if args.global_flag else "工作区"
        print(f"✅ 已删除 {args.tool} ({level})")
        return 0
    print(f"❌ 未找到 {args.tool} 的配置")
    return 1


def cmd_path(_args: argparse.Namespace) -> int:
    print(f"全局配置: {user_config_path()}")
    print(f"工作区配置: {workspace_config_path()}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="em_skill 工具路径配置管理",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s set openocd /usr/bin/openocd
  %(prog)s set uv4 "C:\\Keil_v5\\UV4\\UV4.exe" --global
  %(prog)s get openocd
  %(prog)s list
  %(prog)s remove openocd
  %(prog)s path
        """,
    )
    sub = parser.add_subparsers(dest="command")

    # set
    p_set = sub.add_parser("set", help="保存工具路径")
    p_set.add_argument("tool", help="工具名称")
    p_set.add_argument("path", help="工具路径")
    p_set.add_argument("--global", dest="global_flag", action="store_true", help="保存到全局配置")

    # get
    p_get = sub.add_parser("get", help="查询工具路径")
    p_get.add_argument("tool", help="工具名称")

    # list
    sub.add_parser("list", help="列出所有已配置的工具")

    # remove
    p_rm = sub.add_parser("remove", help="删除配置项")
    p_rm.add_argument("tool", help="工具名称")
    p_rm.add_argument("--global", dest="global_flag", action="store_true", help="从全局配置删除")

    # path
    sub.add_parser("path", help="显示配置文件路径")

    args = parser.parse_args()
    if not args.command:
        parser.print_help()
        return 1

    dispatch = {
        "set": cmd_set,
        "get": cmd_get,
        "list": cmd_list,
        "remove": cmd_remove,
        "path": cmd_path,
    }
    return dispatch[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
