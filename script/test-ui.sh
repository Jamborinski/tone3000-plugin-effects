#!/usr/bin/env bash
# Run the UI testbed selftest on an ISOLATED X display (Xvfb), so the
# focus-policy tests (which raise a window and grab OS keyboard focus to
# exercise JUCE's real focus plumbing) cannot steal the user's input on the
# real display (or be polluted by it). Use `xvfb-run` when available, else
# fall back to the live display (with a warning).
set -uo pipefail
cd "$(dirname "$0")/.."

export PATH=/home/jambo/buildkit/cmake-4.2.3-linux-x86_64/bin:/home/jambo/buildkit/hostbin:/home/jambo/buildkit/stage/usr/bin:$PATH
export LD_LIBRARY_PATH=/home/jambo/buildkit/stage/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

BIN=./build-ui/UiTestbed_artefacts/Release/UiTestbed
[ -x "$BIN" ] || { echo "build first: cmake --build build-ui --target UiTestbed"; exit 1; }

ARGS=("$@")
[ ${#ARGS[@]} -eq 0 ] && ARGS=(--selftest)

if command -v xvfb-run >/dev/null 2>&1; then
  echo ">> selftest on an isolated X display (xvfb-run)"
  xvfb-run -a -s "-screen 0 1280x800x24" "$BIN" "${ARGS[@]}"
else
  echo ">> WARNING: xvfb-run not found -- running on the live display $DISPLAY (focus tests will steal keyboard focus)"
  "$BIN" "${ARGS[@]}"
fi
