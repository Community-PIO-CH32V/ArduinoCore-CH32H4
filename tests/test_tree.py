import subprocess
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_tree_is_complete():
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "check_tree.py")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_arduino_api_is_vendored():
    assert (ROOT / "cores/ch32h4/api/ArduinoAPI.h").is_file()
    assert (ROOT / "cores/ch32h4/api/String.h").is_file()


def test_sdk_submodule_is_populated():
    assert (ROOT / "system/ch32h417lib/Peripheral/inc/ch32h417_rcc.h").is_file()
    assert (ROOT / "system/ch32h417lib/Core/core_riscv.h").is_file()
