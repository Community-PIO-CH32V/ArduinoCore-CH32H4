"""The generated package tables, checked against the vendor's own second copy.

No hardware and no datasheet needed: these run on the committed pinout.json.

THE CHECK THAT MATTERS is the first one. `GPIO_IPD_Unused()` in the WCH SDK
ties every pad a package does NOT bond to a defined level, with a case per
package. Its pin lists are therefore the exact complement of the datasheet's
bonded columns, and the two documents are maintained separately -- so agreement
between them is real evidence, where agreement between a generated file and the
generator that wrote it is none.

Run: python -m pytest tests/test_variants.py -q
"""
import json
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VARIANTS = ROOT / "variants"
SDK_GPIO = ROOT / "system" / "ch32h417lib" / "Peripheral" / "src" / "ch32h417_gpio.c"

# The die: PA..PE are complete banks and PF stops at PF14. There is no PF15
# anywhere in the datasheet's pin table.
DIE_PINS = (["P%s%d" % (p, b) for p in "ABCDE" for b in range(16)]
            + ["PF%d" % b for b in range(15)])

PINOUTS = sorted(VARIANTS.glob("*/pinout.json"))

# WHERE THE TWO VENDOR DOCUMENTS DISAGREE, and which one wins.
#
# CH32H415REU6: the datasheet's Table 2-1-3 gives PE0 pad 54 and gives PE15 no
# pad at all. GPIO_IPD_Unused() has it the other way round, tying PE0 down as
# unused and leaving PE15 alone. Both agree the part bonds 54 GPIO, so it is a
# transposition of PE0 and PE15 in one of them.
#
# The datasheet wins because its pin table closes and the SDK's cannot. Every
# pad position of the QFN60 is accounted for with PE0 at 54: 54 GPIO plus
# seven supply and analogue pads (0, 5, 6, 7, 8, 13, 31), no gaps. If PE15
# were bonded instead it would need a pad number, and there is none free.
#
# Listed per part rather than waved away, so any OTHER disagreement still
# fails. Re-check this if the SDK submodule is updated.
KNOWN_CONFLICTS = {
    "CH32H415REU6": {
        "datasheet_bonded": {"PE0"},
        "sdk_bonded": {"PE15"},
    },
}


def load(path):
    return json.loads(path.read_text())


def sdk_unused():
    """{package id: {bonded-out pad names it ties down}} from the SDK source.

    Parsed from the vendor file in the submodule rather than from a copy, so
    an SDK update that changes a package's pin list shows up here as a failure
    instead of going unnoticed.
    """
    text = SDK_GPIO.read_text(errors="replace")
    body = text[text.index("void GPIO_IPD_Unused(void)"):]
    out = {}
    # case 0x4171050D: ... GPIO_Pin_1|GPIO_Pin_2 ... GPIO_Init(GPIOA, ...)
    for case in re.finditer(r"case\s+(0x[0-9A-Fa-f]{8}):(.*?)break;", body, re.S):
        pkg = int(case.group(1), 16)
        pins = set()
        chunk = case.group(2)
        # Each assignment/GPIO_Init pair is one port. Walking them in order
        # keeps a port's pin list attached to the right port.
        for grp in re.finditer(
                r"GPIO_Pin\s*=\s*(.*?);\s*.*?GPIO_Init\(GPIO([A-F])\s*,", chunk, re.S):
            bits = [int(b) for b in re.findall(r"GPIO_Pin_(\d+)", grp.group(1))]
            port = grp.group(2)
            pins.update("P%s%d" % (port, b) for b in bits)
        out[pkg] = pins
    return out


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_bonded_set_matches_the_sdk_complement(path):
    """Datasheet and SDK must describe the same package.

    A disagreement is not a style difference: one of the two says a pad is
    connected and the other says it is not, and a pin map built on the wrong
    answer puts a peripheral on a pad that goes nowhere.
    """
    d = load(path)
    pkg = int(d["package_id"], 16)
    unused = sdk_unused()

    bonded = set(d["pins"])
    if pkg not in unused:
        # QFN128 bonds every pad, which is exactly why the SDK has no case for
        # it -- there is nothing to tie down. So the claim to check is that the
        # datasheet agrees the set is complete.
        assert bonded == set(DIE_PINS), (
            "%s has no SDK case, so it must bond the whole die; missing %s"
            % (d["part"], sorted(set(DIE_PINS) - bonded)))
        return

    expected = set(DIE_PINS) - unused[pkg]

    conflict = KNOWN_CONFLICTS.get(d["part"])
    if conflict:
        # Apply the documented resolution, then demand exact agreement on
        # everything else.
        expected = (expected | conflict["datasheet_bonded"]) - conflict["sdk_bonded"]

    assert bonded == expected, (
        "%s: datasheet and SDK disagree.\n"
        "  datasheet says bonded, SDK says unused: %s\n"
        "  SDK says bonded, datasheet says absent:  %s"
        % (d["part"], sorted(bonded - expected), sorted(expected - bonded)))


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_pin_numbers_are_port_times_16_plus_bit(path):
    """The one invariant every part shares, and what makes sketches portable."""
    for name, pin in load(path)["pins"].items():
        port, bit = name[1], int(name[2:])
        assert pin["number"] == (ord(port) - ord("A")) * 16 + bit, name


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_no_pin_the_die_does_not_have(path):
    """Catches a misread name, PF15 above all -- it does not exist."""
    unknown = sorted(set(load(path)["pins"]) - set(DIE_PINS))
    assert not unknown, "not on the die: %s" % unknown


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_every_pad_number_is_unique(path):
    """Two pins on one pad means a row was read into the wrong column."""
    seen = {}
    for name, pin in load(path)["pins"].items():
        pad = pin["pad"]
        assert pad not in seen, "pad %s claimed by %s and %s" % (pad, seen[pad], name)
        seen[pad] = name


# ---- the extracted functions against the hand-verified table ---------------

def af_tokens(text):
    """{"TIM2_CH1": 1, ...} from a datasheet function list.

    A pad that serves a timer channel and that timer's external trigger is
    written as one name, "TIM2_CH1_ETR(AF1)", so it is also recorded under the
    two names it actually provides. Without that, three entries of the
    hand-verified QEU6 table look unconfirmed when they are simply spelled
    differently.
    """
    out = {}
    for name, af in re.findall(r"([A-Z0-9_]+)\(AF(\d+)\)", text):
        out[name] = int(af)
        if name.endswith("_ETR") and "_CH" in name:
            head, _, _ = name.rpartition("_ETR")
            out[head] = int(af)
            out[head.split("_CH")[0] + "_ETR"] = int(af)
    return out


def test_the_verified_qeu6_pwm_table_agrees_with_the_datasheet():
    """The one table that has been exercised on hardware, checked both ways.

    variants/CH32H417QEU6/pin_map_package.c came from the MicroPython port
    for this silicon and has run on the board. Agreeing with a table extracted
    independently from the datasheet says two things at once: that the working
    table is right, and that the extraction it is being compared against can
    be trusted for the four parts nobody here can test on hardware.
    """
    src = (VARIANTS / "CH32H417QEU6" / "pin_map_package.c").read_text()
    pins = load(VARIANTS / "CH32H417QEU6" / "pinout.json")["pins"]

    body = src[src.index("g_pwm_af_map[]"):]
    body = body[:body.index("};")]
    entries = re.findall(
        r"\{\s*(P[A-F]\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*,\s*(\d+)\s*\}",
        body)
    assert len(entries) > 100, "the PWM table did not parse"

    missing = []
    for name, tim, ch, negated, af in entries:
        signal = "TIM%s_CH%s%s" % (tim, ch, "N" if negated == "true" else "")
        have = af_tokens(pins.get(name, {}).get("af", ""))
        if have.get(signal) != int(af):
            missing.append("%s %s(AF%s), datasheet says AF%s"
                           % (name, signal, af, have.get(signal)))
    assert not missing, "not confirmed by the datasheet:\n  " + "\n  ".join(missing)


# ---- the resource table, a third independent source ------------------------

def resource(res, needle):
    for label, value in res.items():
        if needle.lower() in label.lower():
            return value
    return ""


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_pin_count_matches_the_resource_table(path):
    """"GPIO port number" in the datasheet's own summary table.

    A third source, independent of both the pin table this was extracted from
    and the SDK function it is checked against. All three agreeing on 95, 65,
    50, 48 and 54 is what makes the extraction believable for the four parts
    that cannot be tried on hardware here.
    """
    d = load(path)
    stated = resource(d["resources"], "GPIO port number")
    assert stated, "no GPIO count in the resource table for %s" % d["part"]
    assert len(d["pins"]) == int(stated), (
        "%s: extracted %d pins, resource table says %s"
        % (d["part"], len(d["pins"]), stated))


@pytest.mark.parametrize("path", PINOUTS, ids=lambda p: p.parent.name)
def test_analog_input_count_matches_the_resource_table(path):
    """The ADC row reads "16+2": external channels plus the two internal ones.

    Counting the ADC_INn entries found on this package's pins has to come to
    the external figure. A mismatch means either a pin was misread or an
    analogue pad was attributed to the wrong package.
    """
    d = load(path)
    stated = resource(d["resources"], "Channels")
    if "+" not in stated:
        pytest.skip("no channel count for %s" % d["part"])
    external = int(stated.split("+")[0])

    # Anchored: "HSADC_IN0" contains "ADC_IN0" and is a different converter.
    found = {name for name, p in d["pins"].items()
             if re.search(r"(?<![A-Z])ADC_IN\d", p["af"])}
    assert len(found) == external, (
        "%s: %d pins carry an ADC input, resource table says %s"
        % (d["part"], len(found), stated))
