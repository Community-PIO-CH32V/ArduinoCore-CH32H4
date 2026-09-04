#!/usr/bin/env python3
"""Generate package_ch32h4_index.json, the Boards Manager index.

    python tools/makeindex.py                 # refresh the index
    python tools/makeindex.py --platform-archive ch32h4-1.0.0.zip

An Arduino package index is a list of things to download and the SHA-256 of
each. Writing those by hand is how an index ends up pointing at an archive
nobody can verify, so every checksum here is computed from the actual bytes,
either by asking a registry that already knows them or by fetching the file.

WHY THIS EXISTS AT ALL. Without an index, platform.txt has to resolve its
tools from somewhere, and the only "somewhere" a checkout has is the machine
it is on -- a compiler on PATH, a Python on PATH. arduino-pico does not do
that: it declares a pqt-python3 tool and its platform.txt says, in a comment,
that the property points at the bundled interpreter for board-manager
installs and at a checkout-local one for git. This is the same arrangement for
this core, so that installing it from Boards Manager needs nothing preinstalled
-- which was the whole complaint that prompted it.

WHERE THE ARCHIVES COME FROM. Nothing here is repackaged or re-hosted except
the platform itself, because everything else already exists as a stable public
archive:

  riscv-wch-elf-gcc   codeload zips of three toolchain git repos, one per
                      OS, each pinned to a commit. WCH's own Arduino index
                      does exactly this for its riscv-none-embed-gcc and
                      openocd, so the pattern is the one this ecosystem
                      already relies on.
  openocd-riscv-wch   PlatformIO's registry, which publishes a size and a
                      sha256 per host through its API -- no download needed to
                      write the index, and four hosts instead of one.
  wlink               release assets from our fork, built by its CI. Every
                      host from one build, including the Intel macOS one
                      upstream dropped when GitHub retired its last Intel
                      runner.
  python3             python.org's Windows embeddable distribution, from the
                      permanent /ftp/python/ path. WINDOWS ONLY, deliberately:
                      see the note on that entry.

The platform archive is the exception and must be built and uploaded, because
GitHub's source archives do not contain submodules and five of this core's
dependencies are submodules -- an index pointing at a codeload zip of this
repository would install a core with no SDK, no ArduinoCore-API and no TinyUSB.
"""
import argparse
import hashlib
import io
import json
import os
import sys
import urllib.request
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX = os.path.join(ROOT, "package_ch32h4_index.json")
CACHE = os.path.join(ROOT, ".index-cache")

PACKAGE_NAME = "ch32h4"
MAINTAINER = "Community-PIO-CH32V"
WEBSITE = "https://github.com/Community-PIO-CH32V/ArduinoCore-CH32H4"
EMAIL = "maxi.gerhardt@googlemail.com"

PLATFORM_VERSION = "1.0.0"
# Assets that have to exist on the release this index points at. repack()
# fills it in; main() prints it, because an index naming a file nobody uploaded
# is an index that fails at install time on somebody else's machine.
PUBLISH = []

RELEASE_BASE = ("https://github.com/Community-PIO-CH32V/ArduinoCore-CH32H4/"
                "releases/download/%s/" % PLATFORM_VERSION)
RELEASE_ASSET = RELEASE_BASE + "%s"

PLATFORM_ARCHIVE_URL = (
    "https://github.com/Community-PIO-CH32V/ArduinoCore-CH32H4/releases/"
    "download/%s/ch32h4-%s.zip" % (PLATFORM_VERSION, PLATFORM_VERSION))

# Arduino's host triples. A tool missing an entry for the running host is not
# an error in the index -- arduino-cli simply reports that the platform is
# unavailable there -- so an incomplete list is a supported-platform decision,
# not a bug, and is called out where it is made.
WIN = ["x86_64-mingw32", "i686-mingw32"]
LINUX = ["x86_64-pc-linux-gnu"]
LINUX_ARM = ["aarch64-linux-gnu"]
MAC_X86 = ["x86_64-apple-darwin"]
MAC_ARM = ["arm64-apple-darwin"]

# PlatformIO's system names, mapped onto Arduino's.
PIO_HOSTS = {
    "windows_amd64": WIN,
    "linux_x86_64": LINUX,
    "linux_aarch64": LINUX_ARM,
    "darwin_x86_64": MAC_X86,
    "darwin_arm64": MAC_ARM,
}

TOOLS = [
    {
        "name": "riscv-wch-elf-gcc",
        "version": "12.2.0",
        # Three repositories, one per OS, each a git repo holding the built
        # toolchain and each archived by GitHub. The zip has a single root
        # directory, which arduino-cli strips, so {runtime.tools.*.path}/bin is
        # where the compiler lands -- every one of them has bin/, lib/ and
        # riscv-wch-elf/ at its root.
        #
        # PINNED TO COMMITS ON THE gcc12 BRANCH, and NOT to any repository's
        # 12.2.0 tag, which is a different toolchain: the Windows repo's tag
        # carries riscv-none-elf- binaries and its default branch
        # riscv-none-embed-, while this core needs riscv-wch-elf-. That is not
        # a cosmetic difference -- an upstream RISC-V GCC has no xw extension
        # and cannot assemble -march=rv32imafc_zba_zbb_zbc_zbs_xw at all, so
        # the build would fail on the first instruction rather than on a
        # missing file. A branch name archives whatever it points at today; a
        # commit cannot move.
        #
        # The host lists are the ones each repo's own package.json declares,
        # not a guess: the macOS build says it serves darwin_x86_64 AND
        # darwin_arm64, so one archive covers both Macs.
        "sources": [
            {"hosts": WIN, "repo": "toolchain-riscv-windows",
             "sha": "d2836398c87fdc9832fd04026588c26da199b902"},
            {"hosts": LINUX, "repo": "toolchain-riscv-linux",
             "sha": "fde267fb356efc11dee4c48ea58a1fd6dc787603"},
            {"hosts": MAC_X86 + MAC_ARM, "repo": "toolchain-riscv-mac",
             "sha": "e6360e0f77854a50a653a38ddbde4af413b06453"},
        ],
        "codeload": "Community-PIO-CH32V/%s",
    },
    {
        "name": "openocd-riscv-wch",
        "version": "2.1100.260228",
        # WCH's OpenOCD fork. Upstream OpenOCD has neither the wlinke adapter
        # driver nor the wch_riscv target, so no other build will do.
        #
        # NOT the PlatformIO registry archives, though they are the same build
        # and the registry publishes their checksums for free. arduino-cli
        # REFUSES an archive without exactly one top-level directory:
        #
        #   no unique root dir in archive, found 'bin' and 'contrib'
        #
        # PlatformIO's tarballs are flat, because PlatformIO extracts straight
        # into the package directory. A codeload zip of the same content wraps
        # it in <repo>-<sha>/, which is exactly what arduino-cli wants to
        # strip. The repository keeps one branch per host, all at this version.
        "sources": [
            {"hosts": WIN, "branch": "main",
             "sha": "d506207dad43ef9490c7d847ed8f57dcc78bd674"},
            {"hosts": LINUX, "branch": "linux",
             "sha": "32fb8c1df1567108977e523fffc1dddd426daa62"},
            {"hosts": MAC_X86, "branch": "darwin_x64",
             "sha": "20772531ff69854157a801b0e43b102dda00b226"},
            {"hosts": MAC_ARM, "branch": "darwin_arm",
             "sha": "201d96baae1c9ce1e82416455b2a62f506ba47ee"},
        ],
        "codeload": "Community-PIO-CH32V/tool-openocd-riscv-wch",
    },
    {
        "name": "wlink",
        "version": "0.1.2",
        # From OUR FORK's release, not ch32-rs/wlink's, and every host from the
        # same build.
        #
        # 0.1.2 is required: 0.1.1 reports "Probe is not attached to an MCU" on
        # this part, which is a version problem wearing the costume of a wiring
        # problem, and the hardware suite refuses to run on it. But upstream's
        # 0.1.2 has no Intel macOS asset -- c4f15b7 dropped the target when
        # GitHub retired the macos-13 runner -- and 0.1.1, which does have one,
        # is the broken version.
        #
        # That gap is not "Intel Macs lack an uploader". arduino-cli resolves
        # every toolsDependency or none, so declaring the compiler and OpenOCD
        # for that host while wlink is missing makes the whole platform fail to
        # install there. The fork restores the target by cross-compiling on the
        # Apple Silicon runner, which needs no retired hardware.
        "sources": [
            {"hosts": ["x86_64-mingw32"], "url": "https://github.com/Community-PIO-CH32V/wlink/releases/download/v0.1.2-ch32h4.1/wlink-v0.1.2-ch32h4.1-win-x64.zip"},
            {"hosts": ["i686-mingw32"], "url": "https://github.com/Community-PIO-CH32V/wlink/releases/download/v0.1.2-ch32h4.1/wlink-v0.1.2-ch32h4.1-win-x86.zip"},
            {"hosts": LINUX, "url": "https://github.com/Community-PIO-CH32V/wlink/releases/download/v0.1.2-ch32h4.1/wlink-v0.1.2-ch32h4.1-linux-x64.tar.gz"},
            {"hosts": MAC_ARM, "url": "https://github.com/Community-PIO-CH32V/wlink/releases/download/v0.1.2-ch32h4.1/wlink-v0.1.2-ch32h4.1-macos-arm64.tar.gz"},
            {"hosts": MAC_X86, "url": "https://github.com/Community-PIO-CH32V/wlink/releases/download/v0.1.2-ch32h4.1/wlink-v0.1.2-ch32h4.1-macos-x64.tar.gz"},
        ],
    },
    {
        "name": "python3",
        "version": "3.11.9",
        # WINDOWS ONLY, on purpose, and this is the entry the whole index was
        # written for.
        #
        # Two prebuild hooks are Python: the linker-script substitution and the
        # SDK archive. On Windows there is no interpreter to assume, so one is
        # shipped -- python.org's embeddable distribution, from the permanent
        # /ftp/python/ path, which unpacks flat and carries the whole standard
        # library in python311.zip. The hooks use argparse, hashlib,
        # subprocess and concurrent.futures and nothing else.
        #
        # Linux and macOS get a SHIM rather than an interpreter -- a tiny
        # tarball holding a python3 script that execs the system one. That is
        # what arduino-pico ships as its Unix pqt-python3, and it took a
        # per-host audit of this index to see why it is not merely tidiness:
        #
        # arduino-cli resolves EVERY toolsDependency or none. A tool declared
        # with no entry for the running host does not degrade to "unavailable
        # there" -- it makes `core install` fail on that host outright. Leaving
        # python3 as Windows-only would have made this package uninstallable on
        # Linux and macOS, for the sake of an interpreter those hosts already
        # have.
        #
        # REPACKED, not linked. python.org's embeddable zip has no top-level
        # directory -- python.exe and the .pyds sit at its root -- and
        # arduino-cli refuses an archive like that, the same way it refuses
        # PlatformIO's flat tarballs. So the bytes are taken from python.org,
        # wrapped in one directory, and published as a release asset here. The
        # checksum in the index is of the repacked file, which is the file the
        # release must carry.
        "repack": {
            "url": "https://www.python.org/ftp/python/3.11.9/"
                   "python-3.11.9-embed-amd64.zip",
            "out": "python3-3.11.9-embed-amd64-rooted.zip",
            "root": "python3-3.11.9",
            "hosts": WIN,
        },
        "shim": {
            "out": "python3-3.11.9-via-env.tar.gz",
            "root": "python3-3.11.9",
            "hosts": LINUX + MAC_X86 + MAC_ARM,
        },
    },
]


def hash_file(path):
    """(size, sha256, basename) of a file already on disk."""
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(1 << 20)
            if not chunk:
                break
            size += len(chunk)
            h.update(chunk)
    return size, h.hexdigest(), os.path.basename(path)


def fetch(url, archive_name=None):
    """Return (size, sha256, filename), downloading through a cache.

    Cached because a re-run should not re-download a toolchain to tell you
    nothing changed, and because the whole point of this file is that the
    numbers in it come from the bytes rather than from a guess.
    """
    name = archive_name or url.rsplit("/", 1)[-1]
    path = os.path.join(CACHE, name)
    if not os.path.isfile(path):
        os.makedirs(CACHE, exist_ok=True)
        sys.stderr.write("fetching %s\n" % url)
        tmp = path + ".part"
        req = urllib.request.Request(url, headers={"User-Agent": "makeindex"})
        with urllib.request.urlopen(req) as r, open(tmp, "wb") as fh:
            while True:
                chunk = r.read(1 << 20)
                if not chunk:
                    break
                fh.write(chunk)
        os.replace(tmp, path)

    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as fh:
        while True:
            chunk = fh.read(1 << 20)
            if not chunk:
                break
            size += len(chunk)
            h.update(chunk)
    return size, h.hexdigest(), name


def pio_systems(owner, name, version):
    """Ask PlatformIO's registry for one archive per host.

    The registry already publishes a size and a sha256 for every file it
    serves, so this needs no download at all -- and it covers four hosts where
    fetching each would cost twenty megabytes to learn the same thing.
    """
    url = ("https://api.registry.platformio.org/v3/packages/%s/tool/%s"
           % (owner, name))
    sys.stderr.write("querying %s\n" % url)
    req = urllib.request.Request(url, headers={"User-Agent": "makeindex"})
    with urllib.request.urlopen(req) as r:
        data = json.load(r)

    versions = [data["version"]] + data.get("versions", [])
    for v in versions:
        if v["name"] != version:
            continue
        systems = []
        for f in v["files"]:
            pio_host = "_".join(f["system"]) if isinstance(f.get("system"), list) \
                else f.get("system")
            if isinstance(f.get("system"), list):
                pio_host = f["system"][0]
            hosts = PIO_HOSTS.get(pio_host)
            if not hosts:
                continue
            for host in hosts:
                systems.append({
                    "host": host,
                    "url": f["download_url"],
                    "archiveFileName": f["name"],
                    "checksum": "SHA-256:" + f["checksum"]["sha256"],
                    "size": str(f["size"]),
                })
        if not systems:
            raise SystemExit(
                "makeindex: %s %s has no files for any host this index knows"
                % (name, version))
        return systems
    raise SystemExit("makeindex: %s has no version %s" % (name, version))


def repack(url, out, root):
    """Download a flat archive and wrap its contents in one directory.

    arduino-cli refuses an archive that does not have exactly one top-level
    directory, and several upstreams -- python.org's embeddable build,
    PlatformIO's tool tarballs -- ship flat ones because their own installers
    extract straight into the destination. Rewrapping is the whole fix.

    The result has to be published somewhere, because it is no longer the file
    the upstream URL serves.
    """
    src = os.path.join(CACHE, url.rsplit("/", 1)[-1])
    if not os.path.isfile(src):
        fetch(url)
    dst = os.path.join(CACHE, out)
    if not os.path.isfile(dst):
        sys.stderr.write("repacking %s under %s/\n" % (out, root))
        with zipfile.ZipFile(src) as zin, \
                zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                if info.is_dir():
                    continue
                zout.writestr(root + "/" + info.filename, zin.read(info))
    return dst


SHIM = """#!/bin/sh
# Hand off to the system python3.
#
# This exists so that the python3 tool has an entry for this host. arduino-cli
# resolves every toolsDependency or none, so a tool missing the running host
# does not lose a feature -- it makes the whole platform fail to install.
# Windows genuinely needs an interpreter shipped; Linux and macOS have one, and
# this is the smallest honest way to say so. arduino-pico ships the same thing.
#
# Searched on PATH rather than hardcoded, because python3 is in /usr/bin on
# some systems and a Homebrew or /usr/local prefix on others.
#
# The search splits PATH with IFS rather than with `tr`, for two reasons that
# only showed up when this was actually run. It called an external binary --
# so a caller with a reduced PATH got "tr: command not found" before the real
# diagnostic -- and the unquoted expansion it needed re-split each entry on
# whitespace, which silently skips any PATH directory whose name contains a
# space. IFS=: splits on the one character that separates entries and on
# nothing else.
self=$0
found=
saved_IFS=$IFS
IFS=:
for dir in $PATH; do
    [ -n "$dir" ] || continue
    for name in python3 python; do
        cand="$dir/$name"
        [ -x "$cand" ] || continue
        # Skip this script, in case the tool directory is on PATH ahead of the
        # real interpreter; execing ourselves would loop until the process
        # limit rather than fail.
        [ "$cand" = "$self" ] && continue
        found=$cand
        break
    done
    [ -n "$found" ] && break
done
IFS=$saved_IFS

if [ -n "$found" ]; then
    exec "$found" "$@"
fi

echo "python3 not found on PATH; the CH32H41x Arduino core needs one to" >&2
echo "run its two prebuild hooks. Install python3 and try again." >&2
exit 127
"""


def build_shim(out, root):
    """A tar.gz holding one executable python3 script.

    Built here rather than committed so that the file the index hashes and the
    file the release carries cannot drift apart.
    """
    path = os.path.join(CACHE, out)
    if not os.path.isfile(path):
        os.makedirs(CACHE, exist_ok=True)
        sys.stderr.write("building %s\n" % out)
        data = SHIM.encode("utf-8")
        info = tarfile.TarInfo(root + "/python3")
        info.size = len(data)
        info.mode = 0o755
        info.mtime = 0
        with tarfile.open(path, "w:gz") as tf:
            tf.addfile(info, io.BytesIO(data))
    return path


def build_tools():
    tools = []
    for spec in TOOLS:
        if "pio" in spec:
            systems = pio_systems(*spec["pio"])
        elif "repack" in spec:
            systems = []
            for kind, build in (("repack", None), ("shim", None)):
                if kind not in spec:
                    continue
                r = spec[kind]
                if kind == "repack":
                    path = repack(r["url"], r["out"], r["root"])
                else:
                    path = build_shim(r["out"], r["root"])
                size, digest, name = hash_file(path)
                systems += [{
                    "host": host,
                    "url": RELEASE_ASSET % name,
                    "archiveFileName": name,
                    "checksum": "SHA-256:" + digest,
                    "size": str(size),
                } for host in r["hosts"]]
                PUBLISH.append(path)
        elif "codeload" in spec:
            systems = []
            for src in spec["sources"]:
                repo = spec["codeload"]
                if "%s" in repo:
                    repo = repo % src["repo"]
                url = ("https://codeload.github.com/%s/zip/%s"
                       % (repo, src["sha"]))
                name = "%s-%s-%s.zip" % (spec["name"], spec["version"],
                                         src["sha"][:8])
                size, digest, _ = fetch(url, name)
                for host in src["hosts"]:
                    systems.append({
                        "host": host,
                        "url": url,
                        "archiveFileName": name,
                        "checksum": "SHA-256:" + digest,
                        "size": str(size),
                    })
        else:
            systems = []
            for src in spec["sources"]:
                size, digest, name = fetch(src["url"], src.get("archive"))
                for host in src["hosts"]:
                    systems.append({
                        "host": host,
                        "url": src["url"],
                        "archiveFileName": name,
                        "checksum": "SHA-256:" + digest,
                        "size": str(size),
                    })
        tools.append({
            "name": spec["name"],
            "version": spec["version"],
            "systems": systems,
        })
    return tools


def read_boards():
    """The board names, straight out of boards.txt.

    The index lists them for the Boards Manager card, and a hand-kept second
    list would drift the moment a board is added.
    """
    names = []
    path = os.path.join(ROOT, "boards.txt")
    with io.open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key.count(".") == 1 and key.endswith(".name"):
                names.append({"name": value.strip()})
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--platform-archive",
                    help="the built platform zip, to size and checksum. "
                         "Without it the platform entry keeps whatever the "
                         "committed index already had, so refreshing a tool "
                         "does not silently invalidate the platform.")
    args = ap.parse_args()

    tools = build_tools()

    platform = {
        "name": "CH32H41x Boards",
        "architecture": "ch32h4",
        "version": PLATFORM_VERSION,
        "category": "Contributed",
        "help": {"online": WEBSITE + "/issues"},
        "url": PLATFORM_ARCHIVE_URL,
        "archiveFileName": PLATFORM_ARCHIVE_URL.rsplit("/", 1)[-1],
        "checksum": "SHA-256:" + "0" * 64,
        "size": "0",
        "boards": read_boards(),
        "toolsDependencies": [
            {"packager": PACKAGE_NAME, "name": t["name"], "version": t["version"]}
            for t in tools
        ],
    }

    if args.platform_archive:
        h = hashlib.sha256()
        size = 0
        with open(args.platform_archive, "rb") as fh:
            while True:
                chunk = fh.read(1 << 20)
                if not chunk:
                    break
                size += len(chunk)
                h.update(chunk)
        platform["checksum"] = "SHA-256:" + h.hexdigest()
        platform["size"] = str(size)
    elif os.path.isfile(INDEX):
        with io.open(INDEX, encoding="utf-8") as fh:
            old = json.load(fh)
        for p in old["packages"][0].get("platforms", []):
            if p["version"] == PLATFORM_VERSION:
                platform["checksum"] = p["checksum"]
                platform["size"] = p["size"]
                break

    index = {
        "packages": [{
            "name": PACKAGE_NAME,
            "maintainer": MAINTAINER,
            "websiteURL": WEBSITE,
            "email": EMAIL,
            "help": {"online": WEBSITE + "/issues"},
            "platforms": [platform],
            "tools": tools,
        }]
    }

    with io.open(INDEX, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(index, fh, indent=2, sort_keys=False)
        fh.write("\n")
    sys.stdout.write("wrote %s\n" % INDEX)

    if PUBLISH:
        sys.stdout.write(
            "\nUpload these to the %s release, under exactly these names:\n"
            % PLATFORM_VERSION)
        for path in PUBLISH:
            sys.stdout.write("  %s\n" % path)

    if platform["checksum"].endswith("0" * 64):
        sys.stderr.write(
            "\nmakeindex: the platform archive is still a placeholder. Build\n"
            "it and re-run with --platform-archive, or the index will fail\n"
            "verification on install.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
