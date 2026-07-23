#!/bin/bash
#
# One-time setup for running plox. Creates an isolated virtual environment
# and installs the plox interpreter (and its dependencies) into it, so the
# ./run-plox.sh launcher works without touching your system Python.
#
# Safe to re-run: it reuses the existing environment and reinstalls plox.
#
# Override the interpreter used to build the environment with, e.g.:
#   PYTHON=python3.12 ./setup-plox.sh

set -euo pipefail

# Resolve the repo root from this script's location so it works from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLOX_DIR="$SCRIPT_DIR/plox"
VENV_DIR="$PLOX_DIR/.venv"

PYTHON="${PYTHON:-python3}"

if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "Error: '$PYTHON' was not found. Install Python 3.12+ and retry" >&2
    echo "(or point PYTHON at your interpreter: PYTHON=python3.12 $0)" >&2
    exit 1
fi

# plox requires Python 3.12+ (see pyproject.toml).
if ! "$PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 12) else 1)'; then
    echo "Error: plox needs Python 3.12+, but '$PYTHON' is $("$PYTHON" --version 2>&1)" >&2
    echo "(point PYTHON at a newer interpreter: PYTHON=python3.12 $0)" >&2
    exit 1
fi

echo "Creating virtual environment in $VENV_DIR ..."
"$PYTHON" -m venv "$VENV_DIR"

echo "Installing plox ..."
"$VENV_DIR/bin/pip" install --quiet --upgrade pip
"$VENV_DIR/bin/pip" install --quiet "$PLOX_DIR"

echo
echo "Done. Start plox with:"
echo "  ./run-plox.sh            # interactive REPL"
echo "  ./run-plox.sh program.lox   # run a file"
