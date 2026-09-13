#!/usr/bin/env bash
# Compile the CI examples with PlatformIO, against this checkout.
#
#     bash tests/ci/build-platformio.sh
#
# Through platform-ch32v's default branch and its ch32h417qeu6_evt_r0 board,
# with framework-arduinoch32h4 replaced by a symlink to the checkout -- the
# same arrangement arduino-pico's CI uses with platform-raspberrypi. So this
# also checks that the platform still resolves the core for the board, which
# is exactly what broke before platform-ch32v PR #132 was merged.
#
# Environment:
#   CI_PIO_PLATFORM  platform spec (default: platform-ch32v's GitHub repository)
set -euo pipefail

# `pwd -W` under Git Bash on Windows gives C:/... rather than /c/..., which
# matters because ROOT is embedded in a symlink:// URL below, and Git Bash only
# translates paths that stand alone as arguments. On Linux and macOS -W is not
# an option, so it fails and plain pwd is used.
ROOT="$(cd "$(dirname "$0")/../.." && { pwd -W 2>/dev/null || pwd; })"
PLATFORM="${CI_PIO_PLATFORM:-https://github.com/Community-PIO-CH32V/platform-ch32v.git}"
BOARD="ch32h417qeu6_evt_r0"

failed=()

while IFS= read -r line || [ -n "$line" ]; do
    line="${line%%#*}"
    line="$(echo "$line" | tr -d '\r' | xargs)"
    [ -z "$line" ] && continue

    sketch="$ROOT/$line/$(basename "$line").ino"
    echo "::group::$line"
    if pio ci --board "$BOARD" \
            -O "platform=$PLATFORM" \
            -O "platform_packages=framework-arduinoch32h4@symlink://$ROOT" \
            "$sketch"; then
        echo "::endgroup::"
        echo "PASS  $line"
    else
        echo "::endgroup::"
        echo "::error::FAIL  $line"
        failed+=("$line")
    fi
done < "$ROOT/tests/ci/examples.txt"

echo
if [ "${#failed[@]}" -ne 0 ]; then
    echo "${#failed[@]} build(s) failed:"
    printf '  %s\n' "${failed[@]}"
    exit 1
fi
echo "all PlatformIO builds passed"
