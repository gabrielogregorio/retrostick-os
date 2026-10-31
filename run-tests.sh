#!/usr/bin/env bash
# Single runner for the test suite. Same commands the CI runs.
#
#   ./run-tests.sh   -> lint + unit (C and Python) + e2e (menu and runner, plus the ARM menu when built)
#
# No test opens a window or needs an emulator core or a ROM.
set -u
cd "$(dirname "$0")" || exit

PY="${PY:-.venv/bin/python}"
FAILED=0
declare -a RESULTS

bar() { printf '%s\n' "------------------------------------------------------------"; }
run_layer() {  # run_layer <name> <command...>
  local name="$1"; shift
  bar; echo ">> $name"; bar
  if "$@"; then RESULTS+=("PASS  $name"); else RESULTS+=("FAIL  $name"); FAILED=1; fi
}

run_layer "lint   (ruff)" "$PY" -m ruff check .
if command -v shellcheck > /dev/null; then
  run_layer "lint   (shellcheck)" shellcheck --severity=warning ./*.sh tools/*.sh tools/re/*.sh card-base/retro/init
else
  RESULTS+=("SKIP  lint (shellcheck) - sudo apt install shellcheck")
fi
run_layer "unit   (C, ASan + UBSan)" make test-unit PY="$PY"
run_layer "unit   (Python)" "$PY" -m pytest tests/python
run_layer "e2e    (build)" make test-bins PY="$PY"
run_layer "e2e    (menu, runner, ARM)" "$PY" -m pytest tests/e2e

echo ""; bar; echo "SCOREBOARD"; bar
for r in "${RESULTS[@]}"; do echo "  $r"; done
bar
if [ "$FAILED" -eq 0 ]; then echo "ALL GREEN"; else echo "FAILURES"; fi
exit "$FAILED"
