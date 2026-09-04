"""The Arduino IDE build description, checked without running arduino-cli.

A real IDE build needs arduino-cli, a toolchain and a sketchbook laid out a
particular way, which is too much to ask of a unit test. What these do instead
is check the two things that go wrong silently.

The first is a property nobody defines. The IDE does not object to
"{build.flags.clock}" surviving into a command line; it passes the literal text
to the compiler, which ignores an unknown -D-less argument, and the build
succeeds having quietly dropped whatever the property was for. That is exactly
how the first IDE build of this core came out running at 25 MHz instead of
400 MHz, so every {reference} is resolved here.

The second is boards.txt drifting from its generator. It is generated AND
committed -- the IDE reads it directly and never runs the generator -- so an
edit to the generated file works perfectly until someone re-runs
tools/makeboards.py and silently reverts it.
"""
import io
import pathlib
import re
import subprocess
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLATFORM_TXT = ROOT / "platform.txt"
BOARDS_TXT = ROOT / "boards.txt"

# Everything the IDE supplies itself. Not a guess: these are the properties
# arduino-cli documents as build-time, plus the ones it fills in per recipe.
IDE_PROVIDED = {
    "includes", "source_file", "object_file", "archive_file", "archive_file_path",
    "object_files", "preprocessed_file_path", "build.path", "build.project_name",
    "build.core.path", "build.variant.path", "build.arch", "build.system.path",
    "runtime.platform.path", "runtime.hardware.path", "runtime.ide.path",
    "runtime.ide.version", "runtime.os", "serial.port", "serial.port.file",
    "upload.verbose", "program.verbose", "path", "cmd", "config.path",
}

# Tools resolved from an installed package. A checkout has none, which is what
# platform.local.txt exists to paper over, so they are allowed to be unresolved
# here.
RUNTIME_TOOL = re.compile(r"^runtime\.tools\.")

REFERENCE = re.compile(r"\{([A-Za-z0-9_.]+)\}")


def read_properties(path):
    """key=value, ignoring comments and blank lines. Values may be empty."""
    props = {}
    with io.open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            props[key.strip()] = value
    return props


def board_properties():
    """boards.txt flattened: every property any board or menu option sets.

    Menu options are folded in under the plain property name -- the point is
    which properties CAN be set, not which combination a given FQBN selects.
    """
    props = {}
    for key, value in read_properties(BOARDS_TXT).items():
        if key.startswith("menu."):
            continue
        parts = key.split(".")
        if len(parts) > 3 and parts[1] == "menu":
            # <board>.menu.<menu>.<option>[.<property>...]
            if len(parts) == 4:
                continue        # the option's display name
            props[".".join(parts[4:])] = value
        else:
            props[".".join(parts[1:])] = value
    return props


def test_every_reference_resolves():
    platform = read_properties(PLATFORM_TXT)
    boards = board_properties()

    unresolved = {}
    for key, value in platform.items():
        for ref in REFERENCE.findall(value):
            if ref in IDE_PROVIDED or RUNTIME_TOOL.match(ref):
                continue
            if ref in platform or ref in boards:
                continue
            unresolved.setdefault(ref, []).append(key)

    assert not unresolved, (
        "platform.txt references properties nothing defines: "
        + ", ".join("%s (from %s)" % (ref, ", ".join(sorted(users)))
                    for ref, users in sorted(unresolved.items()))
        + ". The IDE does not error on these -- it passes the literal"
          " {placeholder} to the compiler and builds without whatever it was"
          " for.")


def test_boards_txt_matches_its_generator():
    # Line endings are NOT part of the comparison. makeboards.py writes LF, and
    # git on Windows checks the file out as CRLF under core.autocrlf -- so a
    # byte-exact test passes on the machine that generated the file and fails
    # on every fresh clone, which is the worst possible place to find out.
    original = BOARDS_TXT.read_bytes()
    committed = original.replace(b"\r\n", b"\n")

    proc = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "makeboards.py")],
        capture_output=True)
    assert proc.returncode == 0, proc.stderr.decode("utf-8", "replace")
    regenerated = BOARDS_TXT.read_bytes().replace(b"\r\n", b"\n")

    # Leave the tree exactly as it was found, line endings included.
    BOARDS_TXT.write_bytes(original)

    assert regenerated == committed, (
        "boards.txt does not match tools/makeboards.py. It is generated and"
        " committed, so an edit here survives until the next run of the"
        " generator and then vanishes. Change makeboards.py instead.")


@pytest.mark.parametrize("flag", ["--specs=nano.specs", "--no-relax-gp"])
def test_banned_flags_absent(flag):
    """Both are traps this core has hit and both look like improvements.

    nano.specs rewrites -lstdc++ to -lstdc++_nano, which has no unwind tables,
    so every throw reaches std::terminate after a clean link. --no-relax-gp
    does not exist in GNU ld 2.38 and is a hard link error.
    """
    text = PLATFORM_TXT.read_text(encoding="utf-8")
    for line in text.splitlines():
        if line.strip().startswith("#"):
            continue
        assert flag not in line, "%s must not appear in platform.txt" % flag


def test_clock_macro_is_set_by_every_board():
    """Without one the part runs at 25 MHz off the internal RC.

    ch32h4_clock.c #errors on this now, so a board missing it cannot build --
    but the error names a C file, and the fix is in boards.txt.
    """
    boards = read_properties(BOARDS_TXT)
    ids = sorted({key.split(".")[0] for key in boards if key.endswith(".name")})
    assert ids, "no boards found in boards.txt"
    for board in ids:
        clock = boards.get("%s.build.flags.clock" % board, "")
        assert "SYSCLK_" in clock, (
            "%s does not set build.flags.clock" % board)
