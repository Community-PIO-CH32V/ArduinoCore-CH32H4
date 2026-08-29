#!/usr/bin/env python3
"""Verify the core's directory contract.

Exits non-zero with a list of what is missing. Run by tests/test_tree.py, and
useful on its own after a fresh clone -- the two submodules are easy to forget,
and the failure they produce otherwise is a compiler error deep in an include
chain rather than a statement that the tree is incomplete.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

REQUIRED_DIRS = [
    "cores/ch32h4",
    "cores/ch32h4/api",
    "system/ch32h417lib",
    "variants/CH32H417QEU6",
    "libraries",
    "tools",
    "tests",
]

REQUIRED_FILES = [
    "package.json",
    "README.md",
    "cores/ch32h4/api/ArduinoAPI.h",
    "system/ch32h417lib/Core/core_riscv.h",
    "system/ch32h417lib/Peripheral/inc/ch32h417_rcc.h",
]


def main() -> int:
    missing = [d for d in REQUIRED_DIRS if not (ROOT / d).is_dir()]
    missing += [f for f in REQUIRED_FILES if not (ROOT / f).is_file()]
    if missing:
        print("missing from the tree:")
        for m in missing:
            print("  " + m)
        print("\nIf this is a fresh clone: git submodule update --init --recursive")
        return 1
    print("tree OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
