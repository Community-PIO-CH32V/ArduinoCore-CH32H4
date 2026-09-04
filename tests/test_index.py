"""package_ch32h4_index.json, checked against platform.txt and boards.txt.

The index is what makes this core installable without anything preinstalled,
and every way it goes wrong is quiet. A tool platform.txt references but the
index does not ship resolves to literal "{runtime.tools.x.path}" in a command
line; a checksum with a typo fails at install time on a machine that is not
this one; a boards list that has drifted shows the wrong card in Boards
Manager. None of those is visible from reading the file.

These do not download anything. Whether the URLs serve the bytes the
checksums claim is settled by installing the thing, which is not a unit test's
job -- tools/makeindex.py computes every one of them from the actual file
rather than from a promise.
"""
import io
import json
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
INDEX = ROOT / "package_ch32h4_index.json"
PLATFORM_TXT = ROOT / "platform.txt"
BOARDS_TXT = ROOT / "boards.txt"
MAKEINDEX = ROOT / "tools" / "makeindex.py"

CHECKSUM = re.compile(r"^SHA-256:[0-9a-f]{64}$")
TOOL_REF = re.compile(r"\{runtime\.tools\.([A-Za-z0-9_.-]+)\.path\}")


@pytest.fixture(scope="module")
def index():
    with io.open(INDEX, encoding="utf-8") as fh:
        return json.load(fh)


@pytest.fixture(scope="module")
def package(index):
    packages = index["packages"]
    assert len(packages) == 1, "expected exactly one package"
    return packages[0]


def test_every_tool_platform_txt_uses_is_declared(package):
    """The bug this test exists for.

    platform.txt resolves its compiler, OpenOCD, wlink and Python through
    {runtime.tools.<name>.path}. A name the index does not declare is not an
    error anywhere: arduino-cli leaves the placeholder in the command line and
    the compiler is invoked as a program called "{runtime.tools...}".
    """
    text = PLATFORM_TXT.read_text(encoding="utf-8")
    referenced = set()
    for line in text.splitlines():
        if line.strip().startswith("#"):
            continue
        referenced.update(TOOL_REF.findall(line))

    declared = {t["name"] for t in package["tools"]}
    missing = sorted(referenced - declared)
    assert not missing, (
        "platform.txt uses tools the index does not ship: %s. Installed from "
        "Boards Manager, the placeholder survives into the command line."
        % ", ".join(missing))


def test_tools_dependencies_resolve(package):
    declared = {(t["name"], t["version"]) for t in package["tools"]}
    for platform in package["platforms"]:
        for dep in platform["toolsDependencies"]:
            assert dep["packager"] == package["name"], dep
            assert (dep["name"], dep["version"]) in declared, (
                "%s depends on %s %s, which the index does not declare"
                % (platform["version"], dep["name"], dep["version"]))


def test_every_archive_has_a_real_checksum_and_size(package):
    entries = [("platform " + p["version"], p) for p in package["platforms"]]
    for tool in package["tools"]:
        for system in tool["systems"]:
            entries.append(("%s/%s" % (tool["name"], system["host"]), system))

    for label, entry in entries:
        assert CHECKSUM.match(entry["checksum"]), (
            "%s has a malformed checksum: %r" % (label, entry["checksum"]))
        assert entry["checksum"] != "SHA-256:" + "0" * 64, (
            "%s still has makeindex.py's placeholder checksum -- the archive "
            "was never hashed, and installing it will fail verification"
            % label)
        assert entry["size"].isdigit() and int(entry["size"]) > 0, (
            "%s has a bad size: %r" % (label, entry["size"]))
        assert entry["url"].startswith("https://"), (
            "%s is not served over https: %s" % (label, entry["url"]))


def test_the_archive_file_name_matches_the_url(package):
    """arduino-cli saves the download under archiveFileName.

    Two tools whose URLs differ but whose archiveFileName agrees collide in
    the download cache, and the second install silently gets the first's
    bytes -- then fails its checksum, naming the wrong file.
    """
    seen = {}
    for tool in package["tools"]:
        for system in tool["systems"]:
            name = system["archiveFileName"]
            seen.setdefault(name, set()).add(system["url"])
    for name, urls in sorted(seen.items()):
        assert len(urls) == 1, (
            "%s is used for more than one URL: %s" % (name, sorted(urls)))


def test_windows_gets_a_python_because_it_cannot_be_assumed(package):
    """The entry the index was written for.

    Linux and macOS deliberately have none and fall back to python3 on PATH,
    which is what arduino-pico does too. Windows must not: there is no
    interpreter to assume, and the two prebuild hooks are Python.
    """
    python = [t for t in package["tools"] if t["name"] == "python3"]
    assert python, "the index declares no python3 tool"
    hosts = {s["host"] for s in python[0]["systems"]}
    assert "x86_64-mingw32" in hosts, (
        "no Windows python3: a Boards Manager install would need one on PATH, "
        "which is the whole thing this index removes")


def test_the_platform_version_matches_package_json(package):
    """One artifact, one version number.

    package.json is what PlatformIO installs and this index is what Boards
    Manager installs, and they described the same core with different numbers
    for several releases before anyone noticed -- package.json sat at 0.1.0
    while the index reached 1.0.1. Both now come from package.json, so this
    asks whether the index was regenerated after the version moved.
    """
    declared = json.loads((ROOT / "package.json").read_text(encoding="utf-8"))
    versions = {p["version"] for p in package["platforms"]}
    assert declared["version"] in versions, (
        "package.json says %s but the index has %s -- regenerate it with "
        "tools/makeindex.py" % (declared["version"], sorted(versions)))


def test_the_archive_the_index_names_is_the_one_for_this_version(package):
    """The filename, the URL and the version have to agree.

    They are three separate strings in the generated index, and a release
    that gets two of them right is a download that 404s or, worse, one that
    fetches the PREVIOUS version's archive and installs it under the new
    version's name.
    """
    for plat in package["platforms"]:
        expected = "ch32h4-%s.zip" % plat["version"]
        assert plat["archiveFileName"] == expected, plat["archiveFileName"]
        assert plat["url"].endswith("/%s/%s" % (plat["version"], expected)), (
            plat["url"])


def test_the_board_list_matches_boards_txt(package):
    names = []
    with io.open(BOARDS_TXT, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            if key.count(".") == 1 and key.endswith(".name"):
                names.append(value.strip())

    for platform in package["platforms"]:
        listed = [b["name"] for b in platform["boards"]]
        assert listed == names, (
            "the index lists %s and boards.txt has %s" % (listed, names))
