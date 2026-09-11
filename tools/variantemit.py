"""Turn an extracted pinout into the C files a package variant is made of.

Called by genvariants.py; see that file for where the data comes from.

THE FILTER, AND WHY IT IS NOT FREE GENERATION. The QEU6 peripheral maps in
variants/CH32H417xx_QEU6/pin_map_package.c came from the MicroPython port for
this silicon and have been exercised on the board. tests/test_variants.py
confirms all 127 of their PWM entries against the datasheet independently.

So rather than build each part's maps from scratch, this takes that verified
table and keeps an entry only when BOTH hold for the part in question:

    the pins it names are bonded on that package, and
    that part's OWN datasheet function list confirms the signal at that AF.

The second condition is the one that matters. Copying the table and filtering
by bonded pins alone would advertise SerDes on CH32H415REU6, which has none,
and would keep an AF number that a different part assigns to something else.
Gating on each part's own table cannot do either. What it CAN do is be
incomplete: a pin the part bonds and the QEU6 table never listed will be
missing. That is a far safer failure than a wrong AF, which misconfigures a
pad silently.
"""
import re

# Which signal names each map's rows require, and where.
#
# Each entry: the regex that parses a row, the fields it yields, and a
# function turning those fields into [(pin field, signal name, af field)].
MAPS = {
    "g_pwm_af_map": {
        "row": r"\{\s*(P[A-F]\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*,\s*(\d+)\s*\}",
        "needs": lambda m: [(m[0], "TIM%s_CH%s%s" % (m[1], m[2],
                                                     "N" if m[3] == "true" else ""), int(m[4]))],
        "fmt": lambda m: "    { %-5s, %2s, %s, %-5s, %2s }," % (m[0], m[1], m[2], m[3], m[4]),
    },
}

# The four SPI maps and the two USART maps share one row shape: { id, pin, af }.
for _name, _sig in (("g_spi_sck_map", "SPI%s_SCK"),
                    ("g_spi_miso_map", "SPI%s_MISO"),
                    ("g_spi_mosi_map", "SPI%s_MOSI"),
                    ("g_spi_nss_map", "SPI%s_NSS"),
                    ("g_uart_tx_map", "USART%s_TX"),
                    ("g_uart_rx_map", "USART%s_RX")):
    MAPS[_name] = {
        "row": r"\{\s*(\d+)\s*,\s*(P[A-F]\d+)\s*,\s*(\d+)\s*\}",
        "needs": (lambda sig: lambda m: [(m[1], sig % m[0], int(m[2]))])(_sig),
        "fmt": lambda m: "    { %s, %-5s, %2s }," % (m[0], m[1], m[2]),
    }

MAPS["g_i2c_map"] = {
    "row": r"\{\s*(\d+)\s*,\s*(P[A-F]\d+)\s*,\s*(P[A-F]\d+)\s*,\s*(\d+)\s*\}",
    "needs": lambda m: [(m[1], "I2C%s_SCL" % m[0], int(m[3])),
                        (m[2], "I2C%s_SDA" % m[0], int(m[3]))],
    "fmt": lambda m: "    { %s, %-5s, %-5s, %2s }," % (m[0], m[1], m[2], m[3]),
}

# Peripheral blocks, keyed by their row in the datasheet's "Resource
# differences" table.
#
# PRESENCE COMES FROM THAT TABLE, NOT FROM THE PIN LISTS. The note above
# Table 2-1-1 says the function descriptions "are for all functions and do not
# refer to specific chip models" and sends the reader to the resource table
# instead. Inferring presence from whether a signal appears on some pin
# therefore over-reports: it claimed FSMC on CH32H416RDU6, which has none, and
# QSPI1 on three parts that only have QSPI2.
#
# The block diagrams are no help either, since two of them carry the same
# caption and so one is certainly mislabelled.
# Matched as a SUBSTRING of the row label, because a merged cell in the
# leftmost column makes the label vary: FSMC arrives as "FMC FSMC", SerDes as
# "SerDes(4) SerDes(4)", and the same row is labelled differently on the two
# pages the table spans.
BLOCKS = {
    "ETH": "Ethernet",
    "FSMC": "FSMC",
    "SDRAM": "SDRAM",
    "SDIO": "SDIO",
    "SDMMC": "SDMMC",
    "SERDES": "SerDes",
    "UHSIF": "UHSIF",
    "LTDC": "LTDC",
    "DVP": "DVP",
    "SAI": "SAI",
    "DFSDM": "DFSDM",
    "I3C": "I3C",
    "SWPMI": "SWPMI",
    "PIOC": "PIOC",
    "CMP": "CMP",
    "RNG": "RNG",
    "GPHA": "GPHA",
    "USBSS": "USBSS",
    "USBHS": "USBHS",
    "USBFS": "USBFS/OTG_FS",
    "USBPD": "USBPD",
    "HSADC": "HSADC",
}

# Rows whose value is a count rather than a presence, and the macro to emit.
COUNTS = {
    "USART": "CH32H4_NUM_USART",
    "I2C": "CH32H4_NUM_I2C",
    "CAN": "CH32H4_NUM_CAN",
    "OPA": "CH32H4_NUM_OPA",
}


def resource(res, needle):
    """The cell for the row whose label contains `needle`, or "".

    The first match wins, and the needles are chosen not to overlap: "SDIO"
    does not appear in the SDMMC label, nor "CMP" in any other.
    """
    for label, value in res.items():
        if needle.lower() in label.lower():
            return value
    return ""


def absent(value):
    """True when a resource-table cell says the part does not have the block.

    The table writes absence as a dash and presence as a count, sometimes with
    a qualifier: "1 (QSPI2)", "1(2)", "MAC+10/100M PHY".
    """
    return not value or value.strip() in ("-", "–", "—")


def leading_count(value):
    """The number at the start of a cell, or None. "1 (QSPI2)" -> 1."""
    m = re.match(r"\s*(\d+)", value or "")
    return int(m.group(1)) if m else None


def af_tokens(text):
    """{"TIM2_CH1": 1, ...} from a datasheet function list.

    A pad serving a timer channel and that timer's trigger is written as one
    name, "TIM2_CH1_ETR(AF1)", so it is recorded under the names it really
    provides as well.
    """
    out = {}
    for name, af in re.findall(r"([A-Z0-9_]+)\(AF(\d+)\)", text):
        out[name] = int(af)
        if name.endswith("_ETR") and "_CH" in name:
            head = name.rpartition("_ETR")[0]
            out[head] = int(af)
            out[head.split("_CH")[0] + "_ETR"] = int(af)
    return out


def analog_aliases(template):
    """[("A0", "PC0"), ...] in the order the reference header defines them."""
    return re.findall(r"^#define\s+(A\d+)\s+(P[A-F]\d+)", template, re.M)


def part_signals(pins):
    """{pin name: {signal: af}} for one part."""
    return {name: af_tokens(p["af"]) for name, p in pins.items()}


def has_block(pins, prefix):
    return any(prefix in p["af"] for p in pins.values())


def adc_channels(pins):
    """{pin name: ADC channel} from the ADC_INn entries in the function lists."""
    out = {}
    for name, p in pins.items():
        # (?<![A-Z]) matters: "HSADC_IN0" contains "ADC_IN0", and without
        # the guard every high-speed ADC pad was handed a regular ADC channel.
        # The resource table's channel count is what caught it.
        m = re.search(r"(?<![A-Z])ADC_IN(\d+)", p["af"])
        if m:
            out[name] = int(m.group(1))
    return out


def filter_maps(source, pins):
    """Filter every table in `source` down to what this part can do.

    Returns (text of the tables, {table: (kept, dropped)}).
    """
    sigs = part_signals(pins)
    chunks = []
    stats = {}

    for name, spec in MAPS.items():
        if ("%s[]" % name) not in source:
            continue
        body = source[source.index("%s[]" % name):]
        body = body[:body.index("};")]
        rows = re.findall(spec["row"], body)

        kept = []
        for row in rows:
            ok = True
            for pin, signal, af in spec["needs"](row):
                if pin not in pins or sigs.get(pin, {}).get(signal) != af:
                    ok = False
                    break
            if ok:
                kept.append(row)
        stats[name] = (len(kept), len(rows) - len(kept))

        ctype = ("ch32h4_pwm_af_t" if name == "g_pwm_af_map" else
                 "ch32h4_i2c_pin_t" if name == "g_i2c_map" else
                 "ch32h4_periph_pin_t")
        lines = ["const %s %s[] = {" % (ctype, name)]
        lines += [spec["fmt"](r) for r in kept]
        lines.append("};")
        lines.append("const size_t %s_len = sizeof(%s) / sizeof(%s[0]);"
                     % (name, name, name))
        chunks.append("\n".join(lines))

    return "\n\n".join(chunks) + "\n", stats


def emit_pin_map(part, pins, source):
    text, stats = filter_maps(source, pins)
    head = '''/* Peripheral pin maps for the %(part)s. GENERATED -- see tools/genvariants.py.
 *
 * Derived from the CH32H417QEU6 maps, which came from the MicroPython port for
 * this silicon and have run on hardware, by keeping only the entries this part
 * can actually perform: the pins must be bonded on the %(pkg)s package, and
 * this part's own datasheet function list must confirm the signal at that
 * alternate-function number.
 *
 * An entry the part's table does not confirm is DROPPED rather than adjusted.
 * The table can therefore be incomplete, but it cannot point a peripheral at
 * the wrong pad, which is the failure that would be invisible.
 *
 * Entries kept, by table:
%(stats)s */
#include "Arduino.h"
#include "ch32h4_pinmap.h"

''' % {
        "part": part["part"],
        "pkg": part["package"],
        "stats": "\n".join(" *   %-16s %3d kept, %3d dropped" % (k, v[0], v[1])
                           for k, v in sorted(stats.items())),
    }
    return head + text


def emit_pins_table(part, pins):
    """g_pins[]: port, bit and ADC channel for every pin number.

    Every number is present so indexing is always safe, including for a pad
    this package does not bond -- those carry no ADC channel and are rejected
    by ch32h4_pin_bonded().
    """
    adc = adc_channels(pins)
    lines = []
    for port in "ABCDEF":
        top = 15 if port == "F" else 16
        lines.append("    /* --- Port %s --- */" % port)
        for bit in range(top):
            name = "P%s%d" % (port, bit)
            ch = adc.get(name)
            lines.append("    { GPIO%s, %2d, %4s }," % (
                port, bit, "0xFF" if ch is None else "%d" % ch))
    # PF15 does not exist on the die, but the pin numbering reserves it so that
    # port*16+bit stays exact.
    lines.append("    /* PF15 is not on the die; the slot keeps the numbering "
                 "exact. */")
    lines.append("    { GPIOF, 15, 0xFF },")

    bonded = []
    for n in range(96):
        port, bit = "ABCDEF"[n // 16], n % 16
        bonded.append("P%s%d" % (port, bit) in pins)
    words = []
    for w in range(3):
        v = 0
        for b in range(32):
            if bonded[w * 32 + b]:
                v |= 1 << b
        words.append("0x%08X" % v)

    return '''/* The pin table for the %(part)s. GENERATED -- see tools/genvariants.py.
 *
 * This is SILICON, which is why it lives in the package base rather than in a
 * board variant: port, bit and ADC channel are decided by the die and its
 * bonding, and every board on this part shares them.
 *
 * adc_channel is 0xFF where the pin has no ADC input.
 */
#include "Arduino.h"
#include "ch32h4_gpio.h"

const ch32h4_pin_t g_pins[PINS_COUNT] = {
%(rows)s
};

/* Which pins the %(pkg)s package actually bonds out, one bit per pin number.
 *
 * The names exist on every part so that portable code compiles, but a pad with
 * no bond wire cannot be driven, and pretending otherwise turns a wiring
 * mistake into silence. ch32h4_pin_bonded() reads this. */
const uint32_t g_pin_bonded[3] = { %(bonded)s };
''' % {
        "part": part["part"],
        "pkg": part["package"],
        "rows": "\n".join(lines),
        "bonded": ", ".join(words),
    }


def emit_pins_header(part, pins, template):
    """pins_package.h: names, counts, analogue aliases, domains, blocks.

    The pin-name block and the numbering are identical on every part by
    design, so they are taken from the QEU6 header rather than rewritten. What
    differs per part is appended.
    """
    # Keep only as far as the pin-name block. Everything from the analogue
    # aliases onward is regenerated, and cutting later left the file with the
    # QEU6 aliases AND this part's, plus two NUM_ANALOG_INPUTS.
    cut = template.index("#define A0")
    head = template[:cut].rstrip()
    head = head[:head.rindex("/*")].rstrip()      # drop that block's comment
    head = head.replace("CH32H417xx in the QEU6 package",
                        "%s in the %s package" % (part["part"], part["package"]))
    head = "\n".join(l for l in head.splitlines()
                     if "NUM_ANALOG_INPUTS" not in l)

    adc = adc_channels(pins)
    res = part.get("resources", {})
    blocks = [k for k, needle in sorted(BLOCKS.items())
              if not absent(resource(res, needle))]

    # QSPI needs the qualifier, not just the count: three parts have exactly
    # one QSPI and it is QSPI2, so a bare "has QSPI" would let a sketch reach
    # for QSPI1 and find nothing.
    qspi = resource(res, "QSPI")
    if not absent(qspi):
        if "QSPI2" in qspi:
            blocks.append("QSPI2")
        else:
            blocks += ["QSPI1", "QSPI2"][:leading_count(qspi) or 0]

    lines = [head.rstrip(), ""]
    lines.append("/* GENERATED below this line -- see tools/genvariants.py. */")
    lines.append("")
    lines.append("#define CH32H4_PACKAGE_ID   %s   /* the word at 0x1FFFF704, "
                 "masked & ~0xF0 */" % part["package_id"])
    lines.append("#define CH32H4_PINS_BONDED  %d   /* of %d pin numbers */"
                 % (len(pins), 96))
    lines.append("")

    # THE ALIAS ORDER IS THE QEU6 ORDER, not ADC channel order.
    #
    # Numbering by channel would make A0 the first analogue pin each package
    # happens to bond, so A0 would be PC0 on one part and PA0 on another and a
    # sketch moved between them would read a different pad while compiling
    # cleanly. Keeping the order fixed and simply leaving out an alias whose
    # pin is absent means such a sketch fails to build, which is what should
    # happen when the pin is not there.
    lines.append("/* Analogue inputs, in the CH32H417QEU6's alias order so that")
    lines.append("   A<n> means the same pad on every part. An alias whose pin")
    lines.append("   this package does not bond is left undefined rather than")
    lines.append("   reassigned. */")
    defined = 0
    for alias, name in analog_aliases(template):
        if name in pins:
            lines.append("#define %-4s %-5s /* ADC_IN%s */"
                         % (alias, name, adc.get(name, "?")))
            defined += 1
        else:
            lines.append("/* %-4s %-5s is not bonded on this package */"
                         % (alias, name))
    lines.append("#define NUM_ANALOG_INPUTS    %d" % defined)
    lines.append("")

    # The DACs are fixed pads with no mux, but the COUNT is per part: the
    # resource table gives CH32H417WEU6 "1 (DAC2)", so defining DAC1 there
    # would name a converter the part does not have.
    dac = resource(res, "DAC (Unit)")
    have_dac1 = "PA4" in pins and not absent(dac) and "DAC2)" not in dac
    have_dac2 = "PA5" in pins and not absent(dac)
    lines.append("/* The 12-bit DACs. Fixed pads: there is no mux. */")
    if have_dac1:
        lines.append("#define PIN_DAC1         PA4")
        lines.append("#define DAC1             PIN_DAC1")
        lines.append("#define PIN_DAC_OUT      PIN_DAC1")
    else:
        lines.append("/* No DAC1 on this part (resource table says %r). */"
                     % (dac or "-"))
    if have_dac2:
        lines.append("#define PIN_DAC2         PA5")
        lines.append("#define DAC2             PIN_DAC2")
        if not have_dac1:
            # Something has to answer for the older single-DAC name, and on a
            # part whose only converter is DAC2 that is DAC2.
            lines.append("#define PIN_DAC_OUT      PIN_DAC2")
    lines.append("")

    for row, macro in sorted(COUNTS.items()):
        n = leading_count(resource(res, row))
        if n is not None:
            lines.append("#define %-20s %d" % (macro, n))
    lines.append("")

    lines.append("/* The on-die ADC channels, which have no pad. */")
    lines.append("#define ATEMP  (PINS_COUNT + 0)")
    lines.append("#define AVREF  (PINS_COUNT + 1)")
    lines.append("#define ADC_INTERNAL_TEMP_CHANNEL  16")
    lines.append("#define ADC_INTERNAL_VREF_CHANNEL  17")
    lines.append("#define digitalPinToInterrupt(p)  (p)")
    lines.append("")

    lines.append("/* Which peripheral blocks this part has, from the")
    lines.append("   datasheet's \"Resource differences\" table -- NOT from")
    lines.append("   whether a signal appears on some pin, since the pin table")
    lines.append("   lists functions generically and says so itself.")
    lines.append("")
    lines.append("   A library for hardware the part lacks should #error on")
    lines.append("   these rather than fail on a missing register. */")
    for b in blocks:
        lines.append("#define CH32H4_HAS_%-8s 1" % b)
    lines.append("")

    lines.append(domains_block(part, pins))
    return "\n".join(lines) + "\n"


def domains_block(part, pins):
    """The 3.3 V supply-domain predicate.

    Not copyable between parts: the H416 and H415 have no VIO18 or VDDIO rail
    at all, so every pin on them is a 3.3 V pin, while on the H417 packages
    only PA5-PA7 and PE2-PE6 are.
    """
    if part["part"].startswith("CH32H417"):
        return ("/* Supply domains matter. Only PA5-PA7 and PE2-PE6 sit on the "
                "3.3 V rail;\n   every other pin is on VIO18 and idles well "
                "below 3.3 V, so a 3.3 V\n   peripheral driven from one of them "
                "may not meet its input thresholds. */\n"
                "#define PIN_IS_3V3_DOMAIN(p)     "
                "(((p) >= PA5 && (p) <= PA7) || ((p) >= PE2 && (p) <= PE6))")
    return ("/* This part has no VIO18 or VDDIO rail, so there is only one I/O\n"
            "   domain and every pin is in it. */\n"
            "#define PIN_IS_3V3_DOMAIN(p)     ((void)(p), 1)")
