#!/usr/bin/env bash
# Compile the CI examples with arduino-cli, against this checkout.
#
#     bash tests/ci/build-arduino-cli.sh
#
# WHY INSTALL THE CORE AND THEN THROW IT AWAY. platform.txt finds the compiler
# through {runtime.tools.riscv-wch-elf-gcc.path}, and arduino-cli only defines
# that for tools installed from a Boards Manager index. So the core is
# installed from THIS checkout's package_ch32h4_index.json, which brings
# exactly the tools this commit declares, and the installed copy of the core
# is then replaced by a link to the checkout. What compiles is the commit
# under test, with the toolchain it asks for. arduino-pico's CI links its
# checkout in the same way.
#
# Environment:
#   ARDUINO_CLI    the arduino-cli binary            (default: arduino-cli)
#   CI_WORK        scratch directory for all state   (default: .ci-arduino)
#   CI_SKIP_SETUP  set to 1 to reuse an existing CI_WORK without reinstalling
set -euo pipefail

# `pwd -W` under Git Bash on Windows gives C:/... rather than /c/..., which
# matters because ROOT is embedded in a file:// URL below, and Git Bash only
# translates paths that stand alone as arguments. On Linux and macOS -W is not
# an option, so it fails and plain pwd is used.
ROOT="$(cd "$(dirname "$0")/../.." && { pwd -W 2>/dev/null || pwd; })"
CLI="${ARDUINO_CLI:-arduino-cli}"
WORK="${CI_WORK:-$ROOT/.ci-arduino}"
CFG="$WORK/arduino-cli.yaml"
FQBN="ch32h4:ch32h4:ch32h417qeu6"

# REFUSE TO RUN WITHOUT THE CONFIG. arduino-cli given a --config-file that does
# not exist does not complain: it falls back to the user's default
# installation. On a developer machine that is an Arduino IDE with some older
# release of this core, and every result is then about that release instead of
# the checkout -- which is exactly how a local trial of this script once
# "failed" twelve builds against core 1.0.1.
cli() {
    if [ ! -f "$CFG" ]; then
        echo "::error::arduino-cli config $CFG does not exist; refusing to fall back to a default installation"
        exit 1
    fi
    "$CLI" --config-file "$CFG" "$@"
}

if [ "${CI_SKIP_SETUP:-0}" != "1" ]; then
    mkdir -p "$WORK/data" "$WORK/user" "$WORK/downloads"
    "$CLI" config init --dest-file "$CFG" --overwrite > /dev/null
    cli config set directories.data "$WORK/data"
    cli config set directories.user "$WORK/user"
    cli config set directories.downloads "$WORK/downloads"
    cli config set board_manager.additional_urls "file://$ROOT/package_ch32h4_index.json"

    cli core update-index
    cli core install ch32h4:ch32h4

    # Replace the installed release with the checkout. There is one version
    # directory after a fresh install.
    PLATFORM_DIR="$WORK/data/packages/ch32h4/hardware/ch32h4"
    VERSION="$(ls "$PLATFORM_DIR")"
    rm -rf "${PLATFORM_DIR:?}/$VERSION"
    ln -s "$ROOT" "$PLATFORM_DIR/$VERSION"
    echo "core $VERSION replaced by a link to $ROOT"
fi

cli core list

failed=()

compile() {
    local sketch="$1" fqbn="$2"
    echo "::group::$sketch  ($fqbn)"
    if cli compile --fqbn "$fqbn" --warnings default "$ROOT/$sketch"; then
        echo "::endgroup::"
        echo "PASS  $sketch  ($fqbn)"
    else
        echo "::endgroup::"
        echo "::error::FAIL  $sketch  ($fqbn)"
        failed+=("$sketch ($fqbn)")
    fi
}

while IFS= read -r line || [ -n "$line" ]; do
    line="${line%%#*}"
    line="$(echo "$line" | tr -d '\r' | xargs)"
    [ -z "$line" ] && continue
    compile "$line" "$FQBN"
done < "$ROOT/tests/ci/examples.txt"

# One build with every menu option moved off its default, so a menu entry that
# no longer produces a valid command line is caught. usbstack=none needs the
# UART as Serial, since there is no USB CDC without a USB stack.
compile "libraries/CH32H4/examples/Blink" \
    "$FQBN:usbstack=none,serial=uart,exceptions=Enabled,lto=Disabled,castore=Minimal,fs=0k"

echo
if [ "${#failed[@]}" -ne 0 ]; then
    echo "${#failed[@]} build(s) failed:"
    printf '  %s\n' "${failed[@]}"
    exit 1
fi
echo "all arduino-cli builds passed"
