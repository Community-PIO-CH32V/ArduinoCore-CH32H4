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

from os.path import isdir, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

FRAMEWORK_DIR = platform.get_package_dir("framework-arduinoch32h4")
assert isdir(FRAMEWORK_DIR)

SDK_DIR = join(FRAMEWORK_DIR, "system", "ch32h417lib")
TINYUSB_DIR = join(FRAMEWORK_DIR, "system", "tinyusb", "src")
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

# Which peripheral `Serial` refers to.
#
# USB CDC by default: it needs no extra wiring and is what a user plugging the
# board in expects to find. `Serial1` is always USART1 on PA9/PA10, into the
# WCH-Link's VCP.
#
# The swap is worth keeping. A fault during static initialisation happens
# before USB has enumerated, so a board that only speaks CDC cannot report one
# -- and the porting notes for this silicon are emphatic that silence is the
# worst diagnostic there is.
serial_iface = str(board.get("build.serial", "usb")).lower()
usb_enabled = serial_iface == "usb" or str(board.get("build.usb", "enabled")) == "enabled"

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
      + (["CH32H4_USB", ("CFG_TUSB_MCU", "OPT_MCU_CH32H417"),
          ("CFG_TUSB_OS", "OPT_OS_NONE")] if usb_enabled else [])
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
    ] + ([TINYUSB_DIR] if usb_enabled else []),

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

# TinyUSB. Built in its own environment for the same reason the vendor SDK is:
# it is third-party code, and its warnings would bury ours.
#
# Only the device stack and the USBFS driver are compiled -- the host stack,
# the other portable backends and the class drivers we do not enable would all
# be dead weight, and some of them do not compile for this target at all.
if usb_enabled:
    tusb_env = env.Clone()
    tusb_env.Append(CCFLAGS=["-w"])
    for src, name in (
        (join(TINYUSB_DIR, "tusb.c"), "tusb"),
        (join(TINYUSB_DIR, "common", "tusb_fifo.c"), "tusb_fifo"),
        (join(TINYUSB_DIR, "device", "usbd.c"), "tusb_usbd"),
        (join(TINYUSB_DIR, "class", "cdc", "cdc_device.c"), "tusb_cdc"),
        (join(TINYUSB_DIR, "portable", "wch", "dcd_ch32_usbfs.c"), "tusb_dcd"),
    ):
        env.Append(PIOBUILDFILES=tusb_env.StaticObject(
            join("$BUILD_DIR", "FrameworkTinyUSB", name + ".o"), src))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "FrameworkArduino"),
    CORE_DIR))

# The reset vectors, both vector tables and the interrupt handlers are reached
# only from hardware, never from a call, so --gc-sections and the archiver
# would both drop them.
env.Prepend(_LIBFLAGS="-Wl,--whole-archive ")
env.Append(_LIBFLAGS=" -Wl,--no-whole-archive -lc")

env.Append(LIBS=libs)

