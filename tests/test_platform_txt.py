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


# ---- the debug configuration ---------------------------------------------

DEBUG_DIR = ROOT / "debug"
PROGRAMMERS_TXT = ROOT / "programmers.txt"


def test_programmers_reference_only_defined_properties():
    """programmers.txt can override anything, including into nothing.

    A typo in a property name here does not fail: the override simply lands on
    a name nobody reads, the default in platform.txt stays in effect, and the
    debugger attaches to the wrong core while appearing to honour the choice.
    """
    platform = read_properties(PLATFORM_TXT)
    programmers = read_properties(PROGRAMMERS_TXT)

    ids = sorted({k.split(".")[0] for k in programmers if k.endswith(".name")})
    assert ids, "no programmers found"

    for key in programmers:
        prop = ".".join(key.split(".")[1:])
        if prop in ("name",):
            continue
        # Every other property must be one platform.txt actually consumes.
        assert prop in platform or prop.startswith("program.tool"), (
            "%s sets %r, which nothing in platform.txt reads" % (key, prop))


def test_the_debug_script_exists():
    script = read_properties(PLATFORM_TXT)["debug.server.openocd.script"]
    name = script.rsplit("/", 1)[-1]
    assert (DEBUG_DIR / name).is_file(), (
        "platform.txt points at debug/%s, which does not exist" % name)


def test_the_openocd_config_sets_no_per_target_gdb_port():
    """An explicit port here breaks the Arduino IDE, silently.

    cortex-debug does not use 3333. It asks the OS for two consecutive free
    ports in 50000-52000, hands the first to OpenOCD as -c "gdb_port N", and
    connects GDB to the one for its targetProcessor. A target carrying an
    explicit -gdb-port ignores that -c and keeps what the file said, so the
    IDE connects to a port nothing is listening on.
    """
    for cfg in sorted(DEBUG_DIR.glob("*.cfg")):
        text = cfg.read_text(encoding="utf-8")
        for line in text.splitlines():
            if line.strip().startswith("#"):
                continue
            assert "-gdb-port" not in line, (
                "%s sets an explicit -gdb-port, which the IDE's own "
                "gdb_port is then unable to override" % cfg.name)


def test_the_openocd_config_creates_cpu0_before_cpu1():
    """Creation order is what binds a hart to a target, and only that.

    -coreid is accepted and ignored by this OpenOCD's wch_riscv driver:
    swapping the values changes nothing, omitting them changes nothing. The
    hart follows the order the targets are created in, so cpu.0 must come
    first -- it is the V3F, and it is processor 0 for targetProcessor.

    Reorder them and the cores swap underneath. Nothing reports it: the
    debugger attaches, halts, single-steps and shows source, and the source is
    the other core's.
    """
    for cfg in sorted(DEBUG_DIR.glob("*.cfg")):
        text = cfg.read_text(encoding="utf-8")
        first = text.index("target create $_TARGETNAME.0")
        second = text.index("target create $_TARGETNAME.1")
        assert first < second, (
            "%s creates cpu.1 before cpu.0, which swaps the cores" % cfg.name)


def test_processor_zero_is_never_the_string_zero():
    """cortex-debug compares targetProcessor against 0 with ===.

        createPortName = (e, t="gdbPort") => t + (0 === e ? "" : e.toString())

    arduino-cli emits these values as JSON strings, so "0" is not 0: it names
    "gdbPort0", a key the port map does not have, and GDB is handed
    localhost:undefined. An empty value is falsy and cortex-debug's own
    `targetProcessor || 0` turns it into the number 0, which works. "1" is
    never compared against 0 and is safe as a string.
    """
    key = "debug.cortex-debug.custom.targetProcessor"
    for path in (PLATFORM_TXT, PROGRAMMERS_TXT):
        for k, v in read_properties(path).items():
            if k.endswith(key):
                assert v.strip() != "0", (
                    "%s sets %s to the string \"0\"; leave it empty instead"
                    % (path.name, k))


def test_both_cores_are_reachable_by_programmer():
    """One programmer per core, and the two must not select the same one."""
    programmers = read_properties(PROGRAMMERS_TXT)
    key = "debug.cortex-debug.custom.targetProcessor"
    chosen = {k.split(".")[0]: v.strip()
              for k, v in programmers.items() if k.endswith(key)}
    assert len(chosen) == 2, chosen
    assert len(set(chosen.values())) == 2, (
        "both programmers select the same processor: %r" % chosen)
    assert "1" in chosen.values(), "no programmer selects the V5F"
    assert "" in chosen.values(), "no programmer selects the V3F"


def test_the_platform_declares_two_processors():
    platform = read_properties(PLATFORM_TXT)
    assert platform.get("debug.cortex-debug.custom.numberOfProcessors") == "2", (
        "without numberOfProcessors=2 cortex-debug allocates one port and "
        "targetProcessor is clamped to 0, so the V5F becomes unreachable")
