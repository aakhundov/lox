#!/bin/bash
#
# Launch plox. Runs the interpreter installed by ./setup-plox.sh:
#   ./run-plox.sh              # interactive REPL
#   ./run-plox.sh program.lox  # run a file

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLOX_BIN="$SCRIPT_DIR/plox/.venv/bin/plox"

if [[ ! -x "$PLOX_BIN" ]]; then
    echo "plox is not set up yet. Run ./setup-plox.sh first" >&2
    exit 1
fi

exec "$PLOX_BIN" "$@"
