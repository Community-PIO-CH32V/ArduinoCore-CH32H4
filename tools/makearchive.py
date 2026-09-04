#!/usr/bin/env python3
"""Build the platform archive that package_ch32h4_index.json points at.

    python tools/makearchive.py                  # -> ch32h4-<version>.zip
    python tools/makearchive.py --out some.zip

The file list comes from `git ls-files --recurse-submodules`, which is the
whole reason this is a script and not a zip command.

WHAT THAT BUYS. GitHub's own source archives do not contain submodules, and
five of this core's dependencies are submodules -- the WCH SDK,
ArduinoCore-API, the TinyUSB fork, lwIP and mbedTLS. An index pointing at a
codeload zip of this repository would install a core that has no SDK and
cannot compile a blink sketch, and would do it without an error anywhere in
the process.

WHAT IT KEEPS OUT. Everything untracked, which is the more dangerous half:

  platform.local.txt   hardcodes the paths of whatever machine built the
                       archive. Shipped, it would override compiler.path and
                       the Python tool with directories that do not exist on
                       the user's machine, and the failure would name a path
                       nobody had ever typed.
  .index-cache/        a third of a gigabyte of downloaded toolchain.
  tests/*/.pio/        build output from the hardware suite.
  .git/                the history of five repositories.

The archive has one top-level directory, ch32h4-<version>, because that is
what arduino-cli strips when it installs a platform.
"""
import argparse
import io
import json
import os
import re
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def platform_version():
    """The one version number, from package.json.

    Read from the same place makeindex.py reads it, rather than scraped out
    of makeindex.py's source -- which is what this used to do, and which
    broke silently the moment that constant stopped being a literal.
    """
    path = os.path.join(ROOT, "package.json")
    with io.open(path, encoding="utf-8") as fh:
        version = json.load(fh).get("version")
    if not version:
        raise SystemExit("makearchive: no version in package.json")
    return version


def submodule_paths():
    """Every path any .gitmodules in the tree registers as a submodule.

    Needed to tell an UNINITIALISED submodule from a missing file. git lists
    both as tracked paths that are not there, and they mean opposite things:
    one is a directory nobody checked out, the other is a broken working tree.
    """
    out = subprocess.run(
        ["git", "-C", ROOT, "ls-files", "--recurse-submodules", "-z",
         "*.gitmodules"],
        stdout=subprocess.PIPE, check=True).stdout
    paths = set()
    for rel in (n.decode("utf-8") for n in out.split(b"\0") if n):
        full = os.path.join(ROOT, rel)
        if not os.path.isfile(full):
            continue
        base = os.path.dirname(rel)
        got = subprocess.run(
            ["git", "config", "-f", full, "--get-regexp", r"submodule\..*\.path"],
            stdout=subprocess.PIPE)
        for line in got.stdout.decode("utf-8", "replace").splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2:
                paths.add("/".join(p for p in (base, parts[1].strip()) if p))
    return paths


def untracked_files():
    """Files present, not ignored, and not known to git.

    They are the ones that vanish from the archive without a word. git
    ls-files lists what is TRACKED, which is exactly the right rule for
    keeping platform.local.txt and build output out -- and exactly the wrong
    one for a file that was added five minutes ago and not yet committed.

    That is not hypothetical: the CH32H417 SVD shipped missing this way. It
    was copied into debug/, referenced from platform.txt, tested, packaged and
    released, and the release had everything except the file, because the
    packaging step ran before the commit. Nothing failed -- the debugger
    simply had no registers.
    """
    out = subprocess.run(
        ["git", "-C", ROOT, "ls-files", "--others", "--exclude-standard", "-z"],
        stdout=subprocess.PIPE, check=True).stdout
    return [n.decode("utf-8") for n in out.split(b"\0") if n]


def tracked_files():
    out = subprocess.run(
        ["git", "-C", ROOT, "ls-files", "--recurse-submodules", "-z"],
        stdout=subprocess.PIPE, check=True).stdout
    names = [n.decode("utf-8") for n in out.split(b"\0") if n]
    if not names:
        raise SystemExit("makearchive: git listed no files")
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", help="the zip to write")
    args = ap.parse_args()

    version = platform_version()
    prefix = "ch32h4-%s" % version
    out = args.out or os.path.join(ROOT, prefix + ".zip")

    names = tracked_files()

    stray = untracked_files()
    if stray:
        # Refuse rather than warn. A warning scrolls past in a release script,
        # and the result is an archive that installs and quietly lacks a file.
        raise SystemExit(
            "makearchive: %d file(s) exist but are not tracked, so they would "
            "be LEFT OUT of the archive silently:\n  %s\n"
            "Commit them (or add them to .gitignore) and run this again."
            % (len(stray), "\n  ".join(sorted(stray)[:20])))

    subs = submodule_paths()
    missing = [n for n in names
               if not os.path.isfile(os.path.join(ROOT, n))]

    # An uninitialised submodule appears here as one missing path -- its own --
    # rather than as its contents. Dropping those is right: the ones this core
    # builds against are all initialised (the check below proves it), and the
    # one that is not is mbedTLS's `framework`, a test harness nothing in this
    # build compiles.
    skipped = [n for n in missing if n in subs]
    broken = [n for n in missing if n not in subs]
    if broken:
        # A real gap. Packaging it produces an archive that installs and then
        # fails to compile, which is the failure this script exists to prevent.
        raise SystemExit(
            "makearchive: %d tracked files are missing from the working tree, "
            "starting with %s. Run `git submodule update --init --recursive`."
            % (len(broken), broken[0]))
    if skipped:
        sys.stderr.write("makearchive: skipping %d uninitialised submodule(s): "
                         "%s\n" % (len(skipped), ", ".join(sorted(skipped))))
    names = [n for n in names if n not in missing]

    # Deterministic order, so two builds of the same commit differ only by the
    # timestamps zip records.
    names.sort()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for name in names:
            z.write(os.path.join(ROOT, name), "%s/%s" % (prefix, name))

    size = os.path.getsize(out)
    sys.stdout.write("wrote %s (%d files, %.1f MB)\n"
                     % (out, len(names), size / 1e6))
    sys.stdout.write("now: python tools/makeindex.py --platform-archive %s\n"
                     % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
