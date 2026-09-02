#!/usr/bin/env python3
"""Substitute values into a text file, for the Arduino IDE build.

The Arduino IDE has no equivalent of PlatformIO's build script, so the one
value the linker script needs at build time -- the filesystem size -- has to be
patched in by a prelink hook. arduino-pico solves this exact problem with a
tool of this name and this shape, and platform.txt calls it the same way:

    simplesub.py --input  <template.ld> \\
                 --out    <build.path>/ch32h417.ld \\
                 --sub    __CH32H4_FS_SIZE__ 131072

Kept deliberately dumb: literal string replacement, no templating language, no
expression evaluation. The thing being edited is a linker script that decides
where a filesystem lands relative to a sketch, and a clever substitution
language is a way to get that wrong quietly.

Every --sub token MUST be found. A typo in platform.txt would otherwise leave
the placeholder in place, and the linker's error would name a symbol nobody
wrote rather than the recipe that failed to substitute.
"""
import argparse
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Replace tokens in a file, writing the result elsewhere.")
    parser.add_argument("--input", required=True,
                        help="the template to read")
    parser.add_argument("--out", required=True,
                        help="the file to write")
    parser.add_argument("--sub", nargs=2, action="append", default=[],
                        metavar=("TOKEN", "VALUE"),
                        help="replace TOKEN with VALUE; repeatable")
    args = parser.parse_args()

    try:
        with open(args.input, "r", encoding="utf-8") as fh:
            text = fh.read()
    except OSError as exc:
        sys.stderr.write("simplesub: cannot read %s: %s\n" % (args.input, exc))
        return 1

    for token, value in args.sub:
        if token not in text:
            sys.stderr.write(
                "simplesub: %r does not appear in %s. The recipe and the"
                " template have gone out of step; substituting nothing would"
                " leave the placeholder in the output and fail later, in the"
                " linker, naming a symbol nobody wrote.\n" % (token, args.input))
            return 1
        text = text.replace(token, value)

    try:
        # Written only when it changes, so an unchanged size does not force a
        # relink on every build.
        try:
            with open(args.out, "r", encoding="utf-8") as fh:
                if fh.read() == text:
                    return 0
        except OSError:
            pass
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write(text)
    except OSError as exc:
        sys.stderr.write("simplesub: cannot write %s: %s\n" % (args.out, exc))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
