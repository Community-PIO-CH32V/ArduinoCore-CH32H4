#!/usr/bin/env python3
"""Compile every library example, with PlatformIO or with arduino-cli.

    python tools/buildexamples.py                    # all of them, PlatformIO
    python tools/buildexamples.py Wire SPI           # only these libraries
    python tools/buildexamples.py --ide              # through arduino-cli
    python tools/buildexamples.py --ide Wire

An example that does not compile is worse than no example: it is the first
thing a new user runs, and it is also the only check that this core's libraries
really do present the interface the upstream ones do. Most of these were
adapted from Arduino's, arduino-pico's or the esp8266 core's, and "adapted"
is exactly where an API drifts without anyone noticing.

PlatformIO builds share one project directory so the framework is compiled once
and reused; only the sketch is recompiled per example. arduino-cli gets its own
build path per example for the same reason it always does -- it caches the core
separately.
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIBS = ROOT / "libraries"

PIO_INI = """[env:ch32h417]
platform = symlink://C:/Users/Max/.platformio/platforms/ch32v
board = ch32h417qeu6_evt_r0
framework = arduino
platform_packages =
    framework-arduinoch32h4 @ symlink://%s
""" % ROOT.as_posix()

IDE_FQBN = ("ch32h4dev:ch32h4:ch32h417qeu6:"
            "usbstack=tinyusb,serial=usb,exceptions=Disabled,fs=128k,"
            "lto=Enabled")

# Libraries that are not user-facing: nobody includes them directly, so an
# example would be an example of nothing.
INTERNAL = {"lwip", "mbedtls", "http-parser"}

# Vendored upstream with its own examples, most of them written for specific
# Adafruit boards -- a Feather's neopixel, an nRF's bootloader. Building all of
# them here would report failures that are not this core's to fix. Name it
# explicitly to build them anyway.
BY_REQUEST = {"Adafruit_TinyUSB_Arduino"}


def examples(only):
    found = []
    for lib in sorted(LIBS.iterdir()):
        if not lib.is_dir() or lib.name in INTERNAL:
            continue
        if only and lib.name not in only:
            continue
        if not only and lib.name in BY_REQUEST:
            continue
        exdir = lib / "examples"
        if not exdir.is_dir():
            continue
        # Recursive: Adafruit_TinyUSB_Arduino groups its examples by class,
        # so the sketches are two levels down rather than one.
        for ino in sorted(exdir.rglob("*.ino")):
            if ino.parent.name == ino.stem:
                found.append((lib.name, ino.parent))
    return found


def build_pio(work, ex):
    src = work / "src"
    if src.exists():
        shutil.rmtree(src)
    src.mkdir(parents=True)
    for f in ex.iterdir():
        if f.is_file():
            shutil.copy2(f, src / f.name)
    r = subprocess.run(["pio", "run", "-d", str(work)],
                       capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr)


def build_ide(sb, ex):
    out = sb / ("out-" + ex.name)
    if out.exists():
        shutil.rmtree(out)
    cli = pathlib.Path(r"C:\Program Files\Arduino IDE\resources\app\lib"
                       r"\backend\resources\arduino-cli.exe")
    if not cli.is_file():
        return None, "arduino-cli not found at %s" % cli
    r = subprocess.run([str(cli), "--config-file", str(sb / "cli.yaml"),
                        "compile", "--fqbn", IDE_FQBN,
                        "--build-path", str(out), str(ex)],
                       capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("libs", nargs="*")
    ap.add_argument("--ide", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    todo = examples(set(args.libs))
    if not todo:
        print("no examples found")
        return 1

    if args.ide:
        sb = pathlib.Path(r"C:\Users\Max\temp\ch32h4-ide-sb")
        if not (sb / "cli.yaml").is_file():
            print("no IDE sketchbook at %s -- see the notes in this file" % sb)
            return 2
        work = sb
    else:
        work = ROOT / "tests" / ".examples-build"
        work.mkdir(parents=True, exist_ok=True)
        (work / "platformio.ini").write_text(PIO_INI, encoding="utf-8")

    bad = []
    t0 = time.time()
    for lib, ex in todo:
        name = "%s/%s" % (lib, ex.name)
        start = time.time()
        ok, out = (build_ide(work, ex) if args.ide else build_pio(work, ex))
        took = time.time() - start
        print("%-4s %-46s %5.1fs" % ("ok" if ok else "FAIL", name, took))
        sys.stdout.flush()
        if not ok:
            bad.append((name, out))
        if args.verbose and not ok:
            print(out[-3000:])

    print("\n%d of %d built in %.0fs" % (len(todo) - len(bad), len(todo),
                                         time.time() - t0))
    for name, out in bad:
        print("\n=== %s ===" % name)
        print(out[-2500:])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
