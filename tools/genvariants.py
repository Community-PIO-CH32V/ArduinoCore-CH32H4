#!/usr/bin/env python3
"""Generate the CH32H41x package variants from the datasheet.

    python tools/genvariants.py --datasheet path/to/CH32H417DS0-2.PDF

WHY A GENERATOR. Five parts, about 95 pins each, ten alternate functions per
pin. Transcribed by hand that is thousands of lines in which a wrong digit
compiles perfectly and is invisible until a peripheral comes out of the wrong
pad. Generated, it is one rule applied uniformly, and the output is checked
against a second source that the vendor maintains independently (see
tests/test_variants.py).

WHY THE PDF IS NOT IN THE REPO. It is WCH's document, not ours to
redistribute, and a build must never depend on a file in somebody's Downloads
folder. So this script is committed, its output is committed, and the datasheet
is named on the command line by whoever regenerates.

WHAT IT READS. Section 2.2 "Pin Description", which is three separate tables
because the three part numbers are not the same silicon:

    Table 2-1-1  CH32H417   three pin-number columns, one per package
    Table 2-1-2  CH32H416    one column, H416RDU6
    Table 2-1-3  CH32H415    one column, H415REU6

A `-` in a column means the pad is not bonded on that package. The
alternate-function lists differ between tables, not just the bonding: PE3 on
H417 offers SERDES_TXP and on H415 it does not.

HOW IT READS IT. By coordinate, not by text order. The tables have no ruling
lines that PyMuPDF's find_tables() can see, and the reading order interleaves
columns, so anything derived from raw text order is wrong in ways that look
plausible. Instead: the rotated column headers ("H417QEU6" and friends) give
each pin-number column an x anchor, rows are delimited by the pin-name column,
and every other cell is assigned by which x band it falls in.
"""
import argparse
import collections
import json
import pathlib
import re
import sys

try:
    import fitz                                  # PyMuPDF
except ImportError:
    sys.exit("PyMuPDF is needed: pip install pymupdf")

ROOT = pathlib.Path(__file__).resolve().parents[1]
VARIANTS = ROOT / "variants"

# A pin name in the name column. The die has PA..PE complete and PF up to
# PF14; anything else in this column is a supply or an analogue pad and is not
# a GPIO.
#
# Matched as a PREFIX, because three names carry more than the pin: the
# backup-domain pads read "PC13(4)-RTC..." and so on, with a footnote marker
# and a second function glued on. Anchoring to the whole cell silently dropped
# exactly PC13, PC14 and PC15, which QFN128 certainly bonds -- caught only
# because the count came to 92 where the die has 95.
#
# The negative lookahead keeps PC1 from matching the start of PC13.
PIN_RE = re.compile(r"^P([A-F])(\d{1,2})(?!\d)")

# The rotated package-column headers, e.g. "H417QEU6". Matching these rather
# than hardcoding x positions is what lets one rule serve all three tables.
HDR_RE = re.compile(r"^H4(1[4-7])([A-Z]{3})6$")

# Which table each part comes from, and the package it names.
PARTS = {
    "CH32H417QEU6": {"column": "H417QEU6", "package": "QFN128", "base": "CH32H417xx_QEU6"},
    "CH32H417MEU6": {"column": "H417MEU6", "package": "QFN88", "base": "CH32H417xx_MEU6"},
    "CH32H417WEU6": {"column": "H417WEU6", "package": "QFN68", "base": "CH32H417xx_WEU6"},
    "CH32H416RDU6": {"column": "H416RDU6", "package": "QFN60", "base": "CH32H416xx_RDU6"},
    "CH32H415REU6": {"column": "H415REU6", "package": "QFN60", "base": "CH32H415xx_REU6"},
}

# The package word at 0x1FFFF704, masked the way GPIO_IPD_Unused() masks it
# (& ~0xF0). QEU6 has no case in that function because QFN128 bonds every pad,
# so there are no unused ones to tie down; its id is recorded from hardware.
PACKAGE_ID = {
    "CH32H417QEU6": 0x4170050D,
    "CH32H417MEU6": 0x4171050D,
    "CH32H417WEU6": 0x4172050D,
    "CH32H415REU6": 0x4150050D,
    "CH32H416RDU6": 0x4160050D,
}


def canon(txt):
    """"PC13(4)-RTC..." -> "PC13". None if this cell is not a pin name."""
    m = PIN_RE.match(txt)
    if not m:
        return None
    return "P%s%d" % (m.group(1), int(m.group(2)))


def pin_number(name):
    """PA0 is 0, PB0 is 16, and so on: port index * 16 + bit.

    The same encoding on every part, which is the point -- PB3 is 19 whatever
    the package, so a sketch moves between them unchanged. It is also the
    encoding the MicroPython port for this silicon uses.
    """
    m = PIN_RE.match(name)
    port, bit = m.group(1), int(m.group(2))
    return (ord(port) - ord("A")) * 16 + bit


def find_tables(doc):
    """{table id: [page indices]} for the three pin-definition tables."""
    starts = {}
    for i in range(doc.page_count):
        for m in re.finditer(r"Table (2-1-[123]) CH32H4\d\d Pin definitions",
                             doc[i].get_text()):
            starts.setdefault(m.group(1), i)
    out = {}
    ordered = sorted(starts.items(), key=lambda kv: kv[1])
    for n, (tid, first) in enumerate(ordered):
        # A table runs until the next one starts, or until the pin-function
        # tables (2-2-x) begin.
        last = doc.page_count
        if n + 1 < len(ordered):
            last = ordered[n + 1][1]
        else:
            for j in range(first, doc.page_count):
                if "Table 2-2-1 " in doc[j].get_text():
                    last = j
                    break
        out[tid] = list(range(first, last))
    return out


def column_anchors(page):
    """{column name: x of its pin-number cells}, from the rotated headers."""
    out = {}
    for x0, _y0, _x1, _y1, txt, *_ in page.get_text("words"):
        if HDR_RE.match(txt):
            out[txt] = x0
    return out


def bands(values, tol=2.0):
    """Deduplicate ruling-line positions into ordered [(lo, hi)] bands.

    Each rule is drawn as two strokes a fraction of a point apart, so the raw
    positions come in pairs.
    """
    uniq = []
    for v in sorted(values):
        if not uniq or v - uniq[-1] > tol:
            uniq.append(v)
    return [(uniq[i], uniq[i + 1]) for i in range(len(uniq) - 1)]


def grid(page):
    """(row bands, column bands) from the table's own ruling lines.

    THE RULES ARE WHY THIS IS RELIABLE. PyMuPDF's find_tables() reports nothing
    on these pages, which made it look as though the table had no lines and
    invited guessing at the geometry from text positions. It does have them,
    and guessing does not work: a pad number is vertically CENTRED in its row
    while the function list is top-aligned, and a wrapped pin name sits above
    both, so no single y identifies a row. The lines do.
    """
    hs, vs = [], []
    for d in page.get_drawings():
        for item in d["items"]:
            if item[0] == "l":
                p1, p2 = item[1], item[2]
                if abs(p1.y - p2.y) < 0.6:
                    hs.append(p1.y)
                if abs(p1.x - p2.x) < 0.6:
                    vs.append(p1.x)
            elif item[0] == "re":
                r = item[1]
                if r.height < 1.2:
                    hs.append(r.y0)
                if r.width < 1.2:
                    vs.append(r.x0)
    return bands(hs), bands(vs)


def cell_text(words, rb, cb):
    """The text inside one cell, in reading order."""
    got = [(y0, x0, t) for x0, y0, _x1, _y1, t, *_ in words
           if rb[0] - 1 <= y0 < rb[1] and cb[0] - 1 <= x0 < cb[1]]
    return " ".join(t for _y, _x, t in sorted(got))


def read_table(doc, pages, anchors):
    """[(pin name, {column: pad number or None}, af text)] over a whole table.

    `anchors` comes from the table's first page; continuation pages repeat the
    data in the same columns but not always the header, so they are carried
    forward.
    """
    rows = []
    for pno in pages:
        page = doc[pno]
        words = page.get_text("words")
        rbs, cbs = grid(page)
        if not rbs or not cbs:
            continue

        def column_of(x):
            for i, cb in enumerate(cbs):
                if cb[0] <= x < cb[1]:
                    return i
            return None

        pad_cols = {}
        for col, cx in anchors.items():
            i = column_of(cx + 2)
            if i is not None:
                pad_cols[col] = i
        if not pad_cols:
            continue

        # The name column is found by looking for the names, not by counting
        # bands across from the pad columns: the number of ruling lines varies
        # between pages of the same table, so any fixed offset runs off the
        # end on some page.
        votes = collections.Counter()
        for i, cb in enumerate(cbs):
            if i in pad_cols.values():
                continue
            for x0, _y0, _x1, _y1, txt, *_ in words:
                if cb[0] <= x0 < cb[1] and canon(txt):
                    votes[i] += 1
        if not votes:
            continue
        name_col = votes.most_common(1)[0][0]

        # Everything right of "Main function (After reset)" is a function
        # list: "Pin function(2)" and "Remapping function(3)" on the H417 and
        # H416 tables, "Remapping function(3)" alone on the H415 one. Both are
        # taken, since a remapped function is still a function the pin has.
        af_from = min(name_col + 4, len(cbs))
        for x0, _y0, _x1, _y1, txt, *_ in words:
            if txt == "reset)":
                got = column_of(x0)
                if got is not None:
                    af_from = got + 1
                break

        for rb in rbs:
            name = canon(cell_text(words, rb, cbs[name_col]))
            if not name:
                continue                    # a supply or analogue row
            pads = {}
            for col, ci in pad_cols.items():
                t = cell_text(words, rb, cbs[ci]).strip()
                pads[col] = int(t) if t.isdigit() else None
            af = " ".join(cell_text(words, rb, cb) for cb in cbs[af_from:])
            rows.append((name, pads, af.strip()))
    return rows


def extract(pdf):
    """{part: {"pins": {name: {...}}, ...}} for all five parts."""
    doc = fitz.open(pdf)
    version = "?"
    m = re.search(r"V\d+\.\d+", doc[0].get_text())
    if m:
        version = m.group(0)

    tables = find_tables(doc)
    if len(tables) != 3:
        sys.exit("expected three pin-definition tables, found %s" % list(tables))

    per_column = {}
    table_of = {}
    for tid, pages in sorted(tables.items()):
        anchors = column_anchors(doc[pages[0]])
        if not anchors:
            sys.exit("no package-column headers on page %d" % (pages[0] + 1))
        for name, pads, af in read_table(doc, pages, anchors):
            for col, pad in pads.items():
                per_column.setdefault(col, {})
                # A pin can appear twice if a row wrapped oddly; the first
                # sighting wins and a conflict is reported rather than merged.
                prev = per_column[col].get(name)
                if prev is not None and pad is not None and prev["pad"] != pad:
                    sys.exit("%s: %s has pads %s and %s" %
                             (col, name, prev["pad"], pad))
                if prev is None:
                    per_column[col][name] = {"pad": pad, "af": af}
                elif prev["pad"] is None and pad is not None:
                    per_column[col][name] = {"pad": pad, "af": af or prev["af"]}
        for col in anchors:
            table_of[col] = tid

    out = {}
    for part, meta in PARTS.items():
        col = meta["column"]
        if col not in per_column:
            sys.exit("column %s not found in the datasheet" % col)
        pins = {}
        for name, cell in per_column[col].items():
            if cell["pad"] is None:
                continue                      # `-`: not bonded on this package
            pins[name] = {
                "number": pin_number(name),
                "pad": cell["pad"],
                "af": cell["af"],
            }
        out[part] = {
            "part": part,
            "package": meta["package"],
            "base": meta["base"],
            "package_id": "0x%08X" % PACKAGE_ID[part],
            "source": {
                "file": pathlib.Path(pdf).name,
                "version": version,
                "table": table_of[col],
                "column": col,
            },
            "pins": dict(sorted(pins.items(), key=lambda kv: kv[1]["number"])),
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--datasheet", required=True)
    ap.add_argument("--only", help="one part number, for a quick look")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what was extracted and write nothing")
    args = ap.parse_args()

    data = extract(args.datasheet)
    for part, d in sorted(data.items()):
        if args.only and part != args.only:
            continue
        print("%-14s %-8s %3d pins  (table %s, column %s)" %
              (part, d["package"], len(d["pins"]), d["source"]["table"],
               d["source"]["column"]))
        if args.dry_run:
            continue
        base = VARIANTS / d["base"]
        base.mkdir(parents=True, exist_ok=True)
        (base / "pinout.json").write_text(json.dumps(d, indent=2) + "\n")
        print("               -> %s" % (base / "pinout.json").relative_to(ROOT))


if __name__ == "__main__":
    main()
