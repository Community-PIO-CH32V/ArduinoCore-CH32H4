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
from os import makedirs
from os.path import dirname, isdir, isfile, join

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
LWIP_DIR = join(FRAMEWORK_DIR, "system", "lwip", "src")
MBEDTLS_DIR = join(FRAMEWORK_DIR, "system", "mbedtls")
MBEDTLS_PORT_DIR = join(FRAMEWORK_DIR, "system", "mbedtls-port")
CORE_DIR = join(FRAMEWORK_DIR, "cores", "ch32h4")
variant = board.get("build.variant")
VARIANT_DIR = join(FRAMEWORK_DIR, "variants", variant)

# The package base the variant builds on: pin tables, alternate-function maps
# and the memory layout, shared by every board on the same chip package. The
# variant reaches its headers and its pin table through ordinary #includes,
# which the compiler resolves relative to the including file, so the only
# thing that has to be found here is the linker script.
#
# Found three ways, in order, because PlatformIO board definitions live in the
# platform repository rather than in this core: a board that names its package
# wins, then the variant's own package.txt, then the variant directory itself.
# The middle one is what matters -- it lets a core that has been split into
# package and board build against a board definition that has never heard of
# the split.
def _find_package():
    named = board.get("build.package", "")
    if named:
        return named
    marker = join(VARIANT_DIR, "package.txt")
    if isfile(marker):
        with open(marker) as fh:
            for raw in fh:
                line = raw.strip()
                if line and not line.startswith("#"):
                    return line
    return variant


package = _find_package()
PACKAGE_DIR = join(FRAMEWORK_DIR, "variants", package)

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

# Networking. lwIP plus the on-chip Ethernet MAC and 100M PHY.
#
# Off unless a sketch asks for it: lwIP is roughly 60 KB of flash and 32 KB of
# heap, which a blink sketch should not pay for. Set board_build.network =
# ethernet to turn it on.
network = str(board.get("build.network", "none")).lower()
if network not in ("ethernet", "none"):
    sys.stderr.write("Error: board_build.network must be 'ethernet' or"
                     " 'none', got %r\n" % network)
    env.Exit(1)
net_enabled = network == "ethernet"

# TLS. Mbed TLS 3.6 LTS, with AES on the ECDC block and entropy from the TRNG.
#
# Off unless a sketch asks for it, and for a bigger reason than lwIP's: this is
# about 250 KB of flash and tens of kilobytes of heap per connection. Set
# board_build.tls = mbedtls to turn it on.
#
# It requires networking, but not for a technical reason -- mbedtls itself has
# no idea what a socket is. It is refused without it because a TLS stack with
# nothing to talk to is a quarter of a megabyte of flash doing nothing, and
# almost certainly a typo in the sketch's configuration.
# Link-time optimization.
#
# On by default. The whole image is one compilation unit as far as the
# optimizer is concerned, which on this core matters more than usual: the
# vendor SDK, lwIP and the Arduino layer are built as separate archives, and
# without LTO a one-line SDK accessor called from a sketch is a real function
# call across an archive boundary.
#
# board_build.lto = disabled turns it off. Reasons to: a fault whose backtrace
# is unreadable because everything inlined, or a suspicion that LTO itself is
# the bug -- turning it off is the first thing to try, and having to edit the
# build script to do that would be the wrong place to find out.
lto = str(board.get("build.lto", "enabled")).lower()
if lto not in ("enabled", "disabled"):
    sys.stderr.write("Error: board_build.lto must be 'enabled' or 'disabled',"
                     " got %r\n" % lto)
    env.Exit(1)
lto_enabled = lto == "enabled"

#
# Client AND server, from the one setting. The server half was a separate value
# until it was measured: 29,900 bytes of flash, not one byte of RAM, and not
# collectable -- see the note in ch32h4_mbedtls_config.h for why neither
# --gc-sections nor LTO can drop it. 29 KB is 3% of the sketch region and only
# paid by sketches that already asked for a 250 KB TLS stack, which is not
# worth a build-option axis. "mbedtls-server" is still accepted as an alias so
# configurations written against the old spelling keep working.
tls = str(board.get("build.tls", "none")).lower()
if tls not in ("mbedtls", "mbedtls-server", "none"):
    sys.stderr.write("Error: board_build.tls must be 'mbedtls' or 'none',"
                     " got %r\n" % tls)
    env.Exit(1)
tls_enabled = tls in ("mbedtls", "mbedtls-server")
if tls_enabled and not net_enabled:
    sys.stderr.write("Error: board_build.tls = %s needs"
                     " board_build.network = ethernet\n" % tls)
    env.Exit(1)

# The LittleFS partition, in the flash tail.
#
# The layout, from the bottom of the user area upward:
#
#     0x08000000  the V3F stub                32K
#     0x08008000  the sketch                  912K - filesystem_size
#                 LittleFS                    filesystem_size (may be 0)
#     0x080EC000  EEPROM                      16K, always last
#     0x080F0000  end of the 960K user area
#
# The EEPROM is pinned to the very end so that resizing the filesystem never
# moves it, and a stored setting survives a rebuild. arduino-pico pins its
# 4 KB EEPROM to the top for the same reason; this one is 16 KB because the
# erase page here is 8 KB and it uses two of them alternately.
#
# Off by default. A filesystem nobody asked for is flash taken away from the
# sketch, and LittleFS.begin() says plainly what to set when the region is
# empty.
#
#     board_build.filesystem_size = 128k
FS_ERASE_PAGE = 8 * 1024


def _parse_size(text):
    """Accept 128k, 1m, 0, or a plain byte count."""
    t = str(text).strip().lower().replace(" ", "")
    if not t:
        return 0
    mult = 1
    if t.endswith("k"):
        mult, t = 1024, t[:-1]
    elif t.endswith("m"):
        mult, t = 1024 * 1024, t[:-1]
    elif t.endswith("b"):
        t = t[:-1]
    try:
        return int(float(t) * mult)
    except ValueError:
        return None


fs_size = _parse_size(board.get("build.filesystem_size", 0))
if fs_size is None:
    sys.stderr.write("Error: board_build.filesystem_size is not a size."
                     " Use 0, 128k, or 1m.\n")
    env.Exit(1)
if fs_size < 0 or fs_size % FS_ERASE_PAGE:
    sys.stderr.write(
        "Error: board_build.filesystem_size must be a multiple of %d bytes,"
        " the flash erase page on this part -- LittleFS cannot erase a"
        " partial block. Got %d.\n" % (FS_ERASE_PAGE, fs_size))
    env.Exit(1)

# 912K is the whole sketch region. Leaving nothing for the sketch is refused
# here rather than at the link, where it would be an out-of-range error against
# a region the sketch never mentioned.
FS_MAX = 912 * 1024 - FS_ERASE_PAGE
if fs_size > FS_MAX:
    sys.stderr.write(
        "Error: board_build.filesystem_size of %d leaves no room for the"
        " sketch. The maximum is %d.\n" % (fs_size, FS_MAX))
    env.Exit(1)

# Where the partition lands, mirroring the linker script exactly:
#   the EEPROM occupies the last 16 KB of the 960 KB user area, and the
#   filesystem sits immediately below it.
#
# Published on the environment so the platform's own builder can find it
# without re-deriving the layout -- buildfs and uploadfs need the start address
# and the size, and two places computing the same flash offset from different
# constants is how an image gets written over a sketch.
FLASH_PHYSICAL_BASE = 0x08000000
USER_FLASH_BYTES = 960 * 1024
EEPROM_BYTES = 16 * 1024

fs_start = (FLASH_PHYSICAL_BASE + USER_FLASH_BYTES - EEPROM_BYTES - fs_size)
env["CH32H4_FS_START"] = fs_start
env["CH32H4_FS_SIZE"] = fs_size
env["CH32H4_FS_BLOCK_SIZE"] = FS_ERASE_PAGE
# LittleFS is configured with a 256-byte program page; mklittlefs must build
# the image with the same geometry or the first mount fails.
env["CH32H4_FS_PAGE_SIZE"] = 256

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

# ar and ranlib have to go through the gcc wrappers when LTO is on.
#
# An LTO object holds GIMPLE, not symbols plain ar can see, so a plain ar
# writes an archive index with nothing in it. The link then fails on symbols
# that are demonstrably in the archive -- or, worse, quietly drops an archive
# member whose only definition was one the index did not list. gcc-ar passes
# --plugin, which is the whole difference.
if lto_enabled:
    env.Replace(
        AR=env.subst("$CC").replace("-gcc", "-gcc-ar"),
        RANLIB=env.subst("$CC").replace("-gcc", "-gcc-ranlib"),
    )

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
    ] + (["-flto"] if lto_enabled else []),

    CXXFLAGS=exc_flags + [
        "-fno-threadsafe-statics",
        "-fno-rtti",
        "-fno-use-cxa-atexit",
        "-std=gnu++17",
    ],

    LINKFLAGS=machine_flags + (["-flto"] if lto_enabled else []) + [
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
      + (["CH32H4_NETWORK", "CH32H4_ETHERNET"] if net_enabled else [])
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
        # The AVR compatibility headers -- pgmspace, dtostrf, avr/interrupt.
        # They were vendored with ArduinoCore-API but never reachable, so
        # anything spelling an include the AVR way did not compile. This part
        # has one flat address space, so PROGMEM is empty and pgm_read_* is an
        # ordinary dereference; the headers exist for source compatibility,
        # which is what libraries carried over from the esp8266 lineage need.
        join(CORE_DIR, "api", "deprecated-avr-comp"),
        VARIANT_DIR,
        join(SDK_DIR, "Core"),
        join(SDK_DIR, "Peripheral", "inc"),
    ] + ([
        ADAFRUIT_DIR,
        join(ADAFRUIT_DIR, "arduino"),
    ] if usb_enabled else [])
      + ([
        # cores/ch32h4/lwip carries lwipopts.h and arch/cc.h, and must come
        # before lwIP's own include directory.
        join(CORE_DIR, "lwip"),
        join(LWIP_DIR, "include"),
    ] if net_enabled else [])
      + ([
        # The port directory first: it carries aes_alt.h, which mbedtls
        # includes by that bare name when MBEDTLS_AES_ALT is set.
        MBEDTLS_PORT_DIR,
        join(MBEDTLS_DIR, "include"),
        join(MBEDTLS_DIR, "library"),
    ] if tls_enabled else []),

    LIBSOURCE_DIRS=[join(FRAMEWORK_DIR, "libraries")],
)

# The linker script carries one assignment for the filesystem size, and the
# build rewrites that single line into a copy in the build directory.
#
# Generated rather than passed with --defsym because the size is used in a
# MEMORY region length, and whether a --defsym symbol is visible there depends
# on the ld version. A generated file is also readable after the fact, which a
# linker command line is not.
def _generate_ldscript():
    template = join(PACKAGE_DIR, "ch32h417.ld")
    with open(template) as fh:
        text = fh.read()

    marker = "__CH32H4_FS_SIZE__"
    if marker not in text:
        sys.stderr.write(
            "Error: %s no longer contains the filesystem-size marker the build"
            " rewrites. Someone edited the linker script; the build cannot set"
            " the partition size.\n" % template)
        env.Exit(1)

    # Every occurrence, not the first: the same rule simplesub.py follows, so
    # the two tools cannot disagree about which one counts. (Limiting it to
    # one was a bug -- a mention of the token in a comment above the
    # assignment consumed the substitution, and the link failed on an
    # undefined symbol in the line that mattered.)
    text = text.replace(marker, str(fs_size))

    out = join(env.subst("$BUILD_DIR"), "ch32h417_generated.ld")
    if not isdir(dirname(out)):
        makedirs(dirname(out))
    # Written only when it changes, so an unchanged size does not relink
    # everything on every build.
    if not isfile(out) or open(out).read() != text:
        with open(out, "w") as fh:
            fh.write(text)
    return out


env.Replace(
    LDSCRIPT_PATH=_generate_ldscript(),
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

# lwIP. Third-party, so its own environment with warnings off -- and it is
# large enough that its warnings would drown everything else.
#
# Only the parts NO_SYS=1 uses are compiled: core, core/ipv4, and the netif
# helpers. api/ is the socket and netconn layer, which needs threads and is
# switched off in lwipopts.h; apps/ is a pile of optional protocols.
if net_enabled:
    lwip_env = env.Clone()
    lwip_env.Append(CCFLAGS=["-w"])
    # `core` only: BuildLibrary recurses, so naming core/ipv4 as well compiles
    # every file in it twice and the link fails on multiple definitions.
    libs.append(lwip_env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkLwIP_core"), join(LWIP_DIR, "core")))
    # netif/: only ethernet.c and the ARP glue are wanted. The rest is PPP,
    # SLIP and 6LoWPAN, none of which this board has an interface for.
    for src, name in (
        (join(LWIP_DIR, "netif", "ethernet.c"), "lwip_ethernet"),
    ):
        env.Append(PIOBUILDFILES=lwip_env.StaticObject(
            join("$BUILD_DIR", "FrameworkLwIP_netif", name + ".o"), src))

    # apps/: one of them. SNTP is not optional in the way the rest are --
    # certificate validity is checked against the clock, and a board with no
    # battery has no idea what time it is until something tells it. Named
    # individually rather than by building apps/, which would drag in httpd,
    # mdns, MQTT and the rest.
    env.Append(PIOBUILDFILES=lwip_env.StaticObject(
        join("$BUILD_DIR", "FrameworkLwIP_apps", "sntp.o"),
        join(LWIP_DIR, "apps", "sntp", "sntp.c")))

# Mbed TLS. Its own environment, warnings off, and its own config file.
#
# MBEDTLS_CONFIG_FILE has to reach every translation unit including the
# library's own, so it goes on the whole environment rather than on the mbedtls
# one -- a sketch that includes an mbedtls header must see the same
# configuration the library was built with, or the struct layouts differ and
# the failure is a corrupted context rather than a compile error.
if tls_enabled:
    env.Append(CPPDEFINES=[
        ("MBEDTLS_CONFIG_FILE", r'\"ch32h4_mbedtls_config.h\"'),
        "CH32H4_TLS",
    ])
    mbedtls_env = env.Clone()
    mbedtls_env.Append(CCFLAGS=["-w"])
    libs.append(mbedtls_env.BuildLibrary(
        join("$BUILD_DIR", "FrameworkMbedTLS"), join(MBEDTLS_DIR, "library")))

    # The port layer: the ECDC AES accelerator, and the entropy and clock
    # hooks. Compiled here rather than left in libraries/ for the dependency
    # finder to pick up -- a sketch includes mbedtls/ssl.h, which resolves to
    # the submodule, so the finder never sees a reason to build the port and
    # the link fails on mbedtls_aes_init. Warnings off with the rest of
    # mbedtls: aes_alt.c is vendor-derived and noisy.
    for src, name in (
        (join(MBEDTLS_PORT_DIR, "aes_alt.c"), "aes_alt"),
        (join(MBEDTLS_PORT_DIR, "ch32h4_mbedtls_port.c"), "mbedtls_port"),
    ):
        env.Append(PIOBUILDFILES=mbedtls_env.StaticObject(
            join("$BUILD_DIR", "FrameworkMbedTLS_port", name + ".o"), src))

libs.append(env.BuildLibrary(
    join("$BUILD_DIR", "FrameworkArduino"),
    CORE_DIR))

# NO --whole-archive.
#
# It was here so that the vector tables and the V5F's reset handler survived,
# both of which are reached only from hardware and referenced by nothing. The
# narrow version of that lives in the linker script now -- EXTERN() naming the
# four symbols that genuinely have no caller -- and the broad version had to go
# for the same reason it went from platform.txt: with it, every member of the
# core archive is linked whether or not a sketch uses it, so an unused
# HardwareSerial port costs flash, a receive buffer and a static constructor.
#
# It was also costing something already. Without whole-archive, ch32h4_eh.o
# stops being linked by accident and starts being linked because the linker
# script names its terminate handler -- and the same sketch came out 1.1 KB
# smaller.
env.Append(_LIBFLAGS=" -lc")

env.Append(LIBS=libs)

