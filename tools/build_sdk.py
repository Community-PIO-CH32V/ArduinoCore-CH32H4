#!/usr/bin/env python3
"""Compile the vendor SDK into an archive, for the Arduino IDE build.

arduino-cli compiles cores/{build.core}, variants/{build.variant} and the
libraries a sketch includes. It compiles nothing else. The WCH SDK lives in
system/ch32h417lib, outside all three, so something has to build it -- and a
platform.txt recipe cannot, because there is no recipe that runs over a
directory of sources the IDE does not already know about.

arduino-pico has exactly this problem and solves it by committing a prebuilt
libpico.a. This builds the equivalent instead, from a prebuild hook, for two
reasons: no binaries in the repository, and the SDK is then always compiled
with the same flags as the rest of the build -- including the ones the
Exceptions and Serial menus change, which a prebuilt archive could not follow.

    build_sdk.py --sdk <system/ch32h417lib> --out <build>/libch32h4sdk.a \\
                 --objdir <build>/sdk --cc <gcc> --ar <gcc-ar> \\
                 -- <every compiler flag>

The flags come last, unquoted, after a bare "--". They are NOT a single
quoted argument: {build.defines} contains -DUSB_MANUFACTURER="WCH", whose
inner quotes would end the outer quoting and hand this script a mangled
command line. arduino-cli's own splitter already understands both quote
characters, so letting it do the splitting is both simpler and correct.

Incremental, because a full SDK build is some forty translation units and
nobody should pay for that on every compile of a sketch. An object is rebuilt
when it is missing, when it is older than its source, when it is older than
any header the last compile recorded in its .d file, or when the flags have
changed since the last run.
"""
import argparse
import hashlib
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Two flags the SDK needs and the rest of the build must not have.
#
# -w: under -Wall -Wextra the vendor sources produce hundreds of warnings --
# nested "/*" in the ETH register banners, signed/unsigned comparisons in
# HSEM. They are not ours to fix, and left on they bury a real warning in our
# own code.
#
# -include stddef.h: ch32h417_ecdc.c uses NULL without including anything that
# defines it, so it does not compile on its own. Patching the submodule would
# make a future vendor drop harder to take.
SDK_EXTRA_FLAGS = ["-w", "-include", "stddef.h"]

# Peripheral/src and Core are the two directories with sources in them.
# Startup/ is deliberately absent: this core does not use the vendor's startup
# code -- cores/ch32h4 has its own for both cores, with the prefixed symbols
# that let one ELF hold a V3F image and a V5F image. Ld/ is vendor linker
# scripts, likewise replaced by the variant's.
SDK_SOURCE_DIRS = [os.path.join("Peripheral", "src"), "Core"]


def sources(sdk_dir):
    found = []
    for rel in SDK_SOURCE_DIRS:
        directory = os.path.join(sdk_dir, rel)
        if not os.path.isdir(directory):
            sys.stderr.write("build_sdk: no such directory: %s\n" % directory)
            return None
        for name in sorted(os.listdir(directory)):
            if name.endswith(".c"):
                found.append(os.path.join(directory, name))
    return found


def needs_rebuild(source, obj, dep_file):
    """True if `obj` is missing or older than anything it was built from."""
    try:
        obj_time = os.path.getmtime(obj)
    except OSError:
        return True

    try:
        if os.path.getmtime(source) > obj_time:
            return True
    except OSError:
        return True

    # The .d gcc wrote on the last compile. Without it a changed header is
    # invisible and the stale object survives until someone touches the .c --
    # which, for an SDK header, means every peripheral built against it is
    # quietly wrong.
    try:
        with open(dep_file, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        return True

    # Makefile syntax: "obj: a.h b.h \<newline> c.h". Skip the first two
    # characters before looking for the separating colon, or a Windows target
    # path -- "C:/.../ch32h417_adc.o" -- ends the target at its drive letter.
    idx = text.find(":", 2)
    if idx < 0:
        return True
    body = text[idx + 1:]
    for token in body.replace("\\\n", " ").replace("\\\r\n", " ").split():
        if token == "\\":
            continue
        try:
            if os.path.getmtime(token) > obj_time:
                return True
        except OSError:
            # A header that has since been deleted or a path we cannot stat:
            # rebuild and let the compiler give the real error.
            return True
    return False


def main():
    parser = argparse.ArgumentParser(
        description="Compile the WCH SDK into a static archive.")
    parser.add_argument("--sdk", required=True, help="system/ch32h417lib")
    parser.add_argument("--out", required=True, help="the archive to write")
    parser.add_argument("--objdir", required=True, help="where objects go")
    parser.add_argument("--cc", required=True, help="the C compiler")
    parser.add_argument("--ar", required=True, help="the archiver (gcc-ar)")
    parser.add_argument("flags", nargs=argparse.REMAINDER,
                        help="compiler flags, after a bare --")
    args = parser.parse_args()

    flags = [f for f in args.flags if f != "--"]
    # -MMD is wanted (it is how the header dependencies above get recorded) but
    # -MF/-o from the caller would fight with ours. The IDE's compiler.c.flags
    # carries neither, so this only guards against a hand-edited recipe.
    flags = [f for f in flags if f not in ("-o", "-MF")]
    if "-MMD" not in flags:
        flags.append("-MMD")
    if "-c" not in flags:
        flags.append("-c")
    flags += SDK_EXTRA_FLAGS

    srcs = sources(args.sdk)
    if srcs is None:
        return 1

    os.makedirs(args.objdir, exist_ok=True)

    # A stamp of the command line, so that changing a menu -- Exceptions on,
    # say, or a different Serial -- rebuilds the SDK rather than linking
    # objects compiled against the previous set of defines.
    stamp_path = os.path.join(args.objdir, "flags.stamp")
    stamp = hashlib.sha256(
        ("\n".join([args.cc] + flags)).encode("utf-8")).hexdigest()
    try:
        with open(stamp_path, "r", encoding="utf-8") as fh:
            flags_changed = fh.read().strip() != stamp
    except OSError:
        flags_changed = True

    jobs = []
    for source in srcs:
        # Flattened into one directory. The two source directories have no
        # colliding basenames, and a check is cheaper than mirroring the tree.
        base = os.path.splitext(os.path.basename(source))[0]
        obj = os.path.join(args.objdir, base + ".o")
        dep = os.path.join(args.objdir, base + ".d")
        if flags_changed or needs_rebuild(source, obj, dep):
            jobs.append((source, obj, dep))

    if jobs:
        def compile_one(job):
            source, obj, dep = job
            cmd = [args.cc] + flags + ["-MF", dep, source, "-o", obj]
            proc = subprocess.run(cmd, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT)
            return job, proc.returncode, proc.stdout

        failed = False
        with ThreadPoolExecutor(max_workers=(os.cpu_count() or 2)) as pool:
            for job, code, output in pool.map(compile_one, jobs):
                if output:
                    sys.stderr.write(output.decode("utf-8", "replace"))
                if code != 0:
                    sys.stderr.write("build_sdk: failed to compile %s\n" % job[0])
                    failed = True
                    # A failed object may exist and be half-written; remove it
                    # so the next build does not decide it is up to date.
                    try:
                        os.remove(job[1])
                    except OSError:
                        pass
        if failed:
            return 1

    objects = [os.path.join(args.objdir,
                            os.path.splitext(os.path.basename(s))[0] + ".o")
               for s in srcs]

    # Re-archive only when something was compiled or the archive is gone.
    # `ar rcs` on an unchanged set still rewrites the file, and a new mtime on
    # the archive would relink the sketch on every build.
    if jobs or not os.path.isfile(args.out):
        try:
            os.remove(args.out)
        except OSError:
            pass
        # In one call: gcc-ar takes a long command line happily, and appending
        # per-object would be forty processes.
        proc = subprocess.run([args.ar, "rcs", args.out] + objects,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if proc.stdout:
            sys.stderr.write(proc.stdout.decode("utf-8", "replace"))
        if proc.returncode != 0:
            sys.stderr.write("build_sdk: archiving failed\n")
            return 1

    with open(stamp_path, "w", encoding="utf-8") as fh:
        fh.write(stamp)
    return 0


if __name__ == "__main__":
    sys.exit(main())
