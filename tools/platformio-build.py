# Copyright 2026-present Maximilian Gerhardt <maximilian.gerhardt@rub.de>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Arduino core for the CH32H41x.

One ELF holds both cores: the V3F stub at flash origin and the V5F Arduino
image at CH32_V5F_START_ADDR. See docs/superpowers/specs/ for why that works
here when both prior ports to this silicon needed two.
"""

import sys
from os.path import isdir, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoch32h4")
assert isdir(FRAMEWORK_DIR)

SDK_DIR = join(FRAMEWORK_DIR, "system", "ch32h417lib")
# Adafruit_TinyUSB_Arduino is the USB stack, and our fork of it bundles the
# TinyUSB that knows this part -- OPT_MCU_CH32H417 and the USBFS dcd, neither
# of which is in upstream Adafruit's 0.20. There is deliberately only ONE
# TinyUSB in the build: a second copy alongside it would give two of every
# symbol and, worse, would drift.
ADAFRUIT_DIR = join(FRAMEWORK_DIR, "libraries", "Adafruit_TinyUSB_Arduino", "src")
TINYUSB_DIR = ADAFRUIT_DIR
CORE_DIR = join(FRAMEWORK_DIR, "cores", "ch32h4")
variant = board.get("build.variant")
VARIANT_DIR = join(FRAMEWORK_DIR, "variants", variant)

# The V3F stub owns the first 32 KB of flash; the V5F image starts here.
# NVIC_WakeUp_V5F() masks the address with ~0x3FF without complaining, so an
# unaligned value would start the core in the middle of whatever precedes it.
# The linker script asserts the alignment, and main_v3f.c asserts that this
# constant and the linker agree.
V5F_START_ADDR = "0x00008000"

machine_flags = [
    "-march=%s" % board.get("build.march"),
    "-mabi=%s" % board.get("build.mabi"),
    "-msmall-data-limit=8",
    "-msave-restore",
]

# C++ exceptions are a build option, as in arduino-pico and the ESP cores.
# Unlike arduino-pico, one libstdc++.a serves both settings: -fno-exceptions
# only changes codegen for our own sources, so no second prebuilt library is
# needed. Default off; enabling costs ~30 KB of unwind tables, which stay in
# flash.
exceptions = str(board.get("build.exceptions", "disabled")) == "enabled"
exc_flags = ["-fexceptions"] if exceptions else ["-fno-exceptions"]

# The USB stack. Adafruit TinyUSB, or nothing.
#
#   tinyusb  (default) Adafruit_TinyUSB_Arduino, which owns the descriptor and
#                      provides HID, MSC, MIDI and vendor classes on top of the
#                      CDC that backs `Serial`.
#   none               no USB at all. `Serial` falls back to USART1, and the
#                      TinyUSB sources are not compiled.
usbstack = str(board.get("build.usbstack", "tinyusb")).lower()
if usbstack not in ("tinyusb", "none"):
    sys.stderr.write("Error: board_build.usbstack must be 'tinyusb' or 'none',"
                     " got %r\n" % usbstack)
    env.Exit(1)
usb_enabled = usbstack == "tinyusb"

# Which peripheral `Serial` refers to.
#
# USB CDC when there is USB, because that is what someone plugging the board
# into a PC expects to find. `Serial1` is always USART1 on PA9/PA10, into the
# WCH-Link's VCP.
#
# The swap is worth keeping. A fault during static initialisation happens
# before USB has enumerated, so a board that only speaks CDC cannot report one
# -- and the porting notes for this silicon are emphatic that silence is the
# worst diagnostic there is.
serial_iface = str(board.get("build.serial", "usb" if usb_enabled else "uart")).lower()
if serial_iface == "usb" and not usb_enabled:
    sys.stderr.write("Error: board_build.serial = usb needs"
                     " board_build.usbstack = tinyusb\n")
    env.Exit(1)

env.Append(
    ASFLAGS=machine_flags,
    ASPPFLAGS=["-x", "assembler-with-cpp"],

    CFLAGS=["-std=gnu17"],

    CCFLAGS=machine_flags + [
        "-Os",
        "-Wall",
        "-Wextra",
        # The vendor's ch32h417_eth.h has nested "/*" in its register banner
        # comments, and it is included by anything that touches the SDK. Left
        # on, it produces hundreds of lines per build from a header we do not
        # own, which is exactly how a real warning gets missed.
        "-Wno-comment",
        "-fmessage-length=0",
        "-fsigned-char",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-common",
    ],

    CXXFLAGS=exc_flags + [
        "-fno-threadsafe-statics",
        "-fno-rtti",
        "-fno-use-cxa-atexit",
        "-std=gnu++17",
    ],

    LINKFLAGS=machine_flags + [
        "-Os",
        "-nostartfiles",
        "-Wl,--gc-sections",
        # NOT -Wl,--no-relax-gp. The libhal port needed that flag, but it used
        # lld; this is GNU ld 2.38, which does not have it -- the option was
        # added to binutils later, and passing it is a hard link error here.
        #
        # The hazard it guards against is real but does not apply to this
        # layout: gp relaxation breaks above roughly 305 KB of RAM-resident
        # code, and this core runs XIP from flash with almost none. If a
        # "relocation truncated to fit: R_RISCV_GPREL_I" ever appears, the gp
        # window (+/-2 KB) has overflowed and the fix is
        # -msmall-data-limit=0, not a linker flag.
        # A region on this part will fill up. Say so on every build.
        "-Wl,--print-memory-usage",
        # NOTE: --specs=nano.specs must NEVER appear here. It rewrites
        # -lstdc++ to -lstdc++_nano, which is built without unwind tables, so
        # every throw reaches std::terminate -- after a perfectly clean link,
        # with the failure visible only at run time. It is an attractive flag
        # for the smaller printf it buys; this is what it costs.
        "--specs=nosys.specs",
        '-Wl,-Map="%s"' % join("${BUILD_DIR}", "${PROGNAME}.map"),
    ],

    CPPDEFINES=[
        ("ARDUINO", 10808),
        "ARDUINO_ARCH_CH32H4",
        ("F_CPU", board.get("build.f_cpu")),
        ("CH32_V5F_START_ADDR", V5F_START_ADDR),
    ] + (["CH32H4_EXCEPTIONS"] if exceptions else [])
      + (["CH32H4_USB", "USE_TINYUSB",
          # Adafruit_USBD_Device builds the descriptor from these.
          #
          # NOT the board's hwids: those are 1A86:8010, the WCH-LinkE probe's
          # own identifiers, and the probe sits on the same host. A device
          # sharing them inherits the probe's driver binding -- it enumerates
          # correctly and never becomes a COM port, with no error anywhere.
          # 1209:0001 is pid.codes' generic prototype pair; a shipping board
          # should carry its own allocation.
          ("USB_VID", board.get("build.usb_vid", "0x1209")),
          ("USB_PID", board.get("build.usb_pid", "0x0001")),
          ("USB_MANUFACTURER", env.StringifyMacro(
              board.get("build.usb_manufacturer", board.get("vendor", "WCH")))),
          ("USB_PRODUCT", env.StringifyMacro(
              board.get("build.usb_product", board.get("name", "CH32H417")))),
          ] if usb_enabled else [])
      + ([("CH32H4_SERIAL_IS_USB", 1)] if serial_iface == "usb" else []),

    CPPPATH=[
        # cores/ch32h4/api is deliberately NOT on the include path. On a
        # case-insensitive filesystem it makes <string.h> resolve to
        # ArduinoCore-API's own String.h, and every use of strlen, memcpy and
        # memset in the API then fails to compile. Code reaches the API through
        # "api/ArduinoAPI.h" instead, which is what arduino-pico does.
        CORE_DIR,
        VARIANT_DIR,
        join(SDK_DIR, "Core"),
        join(SDK_DIR, "Peripheral", "inc"),
    ] + ([
        ADAFRUIT_DIR,
        join(ADAFRUIT_DIR, "arduino"),
    ] if usb_enabled else []),

    LIBSOURCE_DIRS=[join(FRAMEWORK_DIR, "libraries")],
)

env.Replace(
    LDSCRIPT_PATH=join(VARIANT_DIR, "ch32h417.ld"),
    SIZEPROGREGEX=r"^(?:\.text|\.rodata|\.itcm_text|\.data)\s+([0-9]+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss)\s+(\d+).*",
)

libs = []

# The variant carries the pin table, which the core's weak defaults must not
# win over, so it is built as sources rather than into an archive.
env.BuildSources(join("$BUILD_DIR", "FrameworkArduinoVariant"), VARIANT_DIR)

# The vendor SDK is third-party code we do not control, and it is built in its
# own environment for two reasons.
#
# -w: it produces hundreds of warnings under -Wall -Wextra -- nested "/*" in
# the ETH register comments, signed/unsigned comparisons in HSEM -- and they
# would bury a real warning in the core's own sources.
#
# -include stddef.h: ch32h417_ecdc.c uses NULL without including anything that
# defines it, so it does not compile on its own. Patching the submodule would
# make it harder to take a future vendor drop.
sdk_env = env.Clone()
sdk_env.Append(CCFLAGS=["-w", "-include", "stddef.h"])

libs.append(sdk_env.BuildLibrary(
    join("$BUILD_DIR", "FrameworkCH32SDK"),
    join(SDK_DIR, "Peripheral", "src")))

libs.append(sdk_env.BuildLibrary(
    join("$BUILD_DIR", "FrameworkCH32SDKCore"),
    join(SDK_DIR, "Core")))

# TinyUSB, from inside the Adafruit fork. Built in its own environment for the
# same reason the vendor SDK is: third-party code whose warnings would bury
# ours.
#
# Only the device stack and the USBFS driver are compiled. The host stack and
# the other vendors' portable backends are dead weight here, and several of
# them do not compile for this target at all.
if usb_enabled:
    tusb_env = env.Clone()
    tusb_env.Append(CCFLAGS=["-w"])
    for src, name in (
        (join(TINYUSB_DIR, "tusb.c"), "tusb"),
        (join(TINYUSB_DIR, "common", "tusb_fifo.c"), "tusb_fifo"),
        (join(TINYUSB_DIR, "device", "usbd.c"), "tusb_usbd"),
        # Every class driver Adafruit can expose. Each is internally gated by
        # its own CFG_TUD_* count, so the ones a sketch does not use compile to
        # nothing -- but they must be present, because usbd.c's driver table
        # references them unconditionally once the count is non-zero.
        (join(TINYUSB_DIR, "class", "cdc", "cdc_device.c"), "tusb_cdc"),
        (join(TINYUSB_DIR, "class", "msc", "msc_device.c"), "tusb_msc"),
        (join(TINYUSB_DIR, "class", "hid", "hid_device.c"), "tusb_hid"),
        (join(TINYUSB_DIR, "class", "midi", "midi_device.c"), "tusb_midi"),
        (join(TINYUSB_DIR, "class", "vendor", "vendor_device.c"), "tusb_vendor"),
        (join(TINYUSB_DIR, "portable", "wch", "dcd_ch32_usbfs.c"), "tusb_dcd"),
    ):
        env.Append(PIOBUILDFILES=tusb_env.StaticObject(
            join("$BUILD_DIR", "FrameworkTinyUSB", name + ".o"), src))

# Adafruit_TinyUSB_Arduino, ARDUINO LAYER ONLY.
#
# The library bundles a complete TinyUSB of its own -- src/class, src/device,
# src/portable, src/tusb.c -- and that copy does not know this part: no
# OPT_MCU_CH32H417, no dcd_ch32_usbfs. So it is repointed at the fork in
# system/tinyusb, which does, by compiling only src/arduino and putting
# system/tinyusb/src ahead of it on the include path so `#include "tusb.h"`
# resolves to the fork. Compiling both copies would give two of every TinyUSB
# symbol.
#
# src/arduino/ports is excluded as well: its ch32 port is guarded on
# ARDUINO_ARCH_CH32 / CH32V20x / CH32V30x and targets the older USB IPs. The
# core supplies the three port functions instead, in ch32h4_usb_adafruit.cpp.
#
# It is compiled unconditionally rather than left to the library dependency
# finder because TinyUSB is the USB stack here: usbd.c's driver table
# references mscd_*, hidd_* and friends as soon as their CFG_TUD_* counts are
# non-zero, and the callbacks behind them live in this layer.
if usb_enabled:
    libs.append(env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkAdafruitTinyUSB"),
        join(ADAFRUIT_DIR, "arduino"),
        src_filter=["+<*>", "-<ports/>"]))

# The framework compiles Adafruit_TinyUSB itself, so the library dependency
# finder must not compile it a second time when a sketch includes
# <Adafruit_TinyUSB.h>. Left to the LDF it walks the whole src/ tree, including
# every other vendor's portable backend -- typec_stm32.c reaching for
# typec/tcd.h is the first thing that stops it -- and anything that did build
# would be a duplicate symbol.
if usb_enabled:
    env_section = "env:" + env["PIOENV"]
    ignored = platform.config.get(env_section, "lib_ignore", [])
    if "Adafruit TinyUSB Library" not in ignored:
        ignored.append("Adafruit TinyUSB Library")
    platform.config.set(env_section, "lib_ignore", ignored)

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "FrameworkArduino"),
    CORE_DIR))

# The reset vectors, both vector tables and the interrupt handlers are reached
# only from hardware, never from a call, so --gc-sections and the archiver
# would both drop them.
env.Prepend(_LIBFLAGS="-Wl,--whole-archive ")
env.Append(_LIBFLAGS=" -Wl,--no-whole-archive -lc")

env.Append(LIBS=libs)

