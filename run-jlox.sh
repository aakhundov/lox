#!/bin/bash
#
# Launch jlox. Runs the interpreter built by ./setup-jlox.sh:
#   ./run-jlox.sh              # interactive REPL
#   ./run-jlox.sh program.lox  # run a file

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JLOX_JAR="$SCRIPT_DIR/jlox/build/jlox.jar"

if [[ ! -f "$JLOX_JAR" ]]; then
    echo "jlox is not built yet. Run ./setup-jlox.sh first" >&2
    exit 1
fi

# Prefer an explicit JAVA_HOME, then the java on PATH. On macOS the
# /usr/bin/java stub can fail to locate a JDK, so fall back to java_home.
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVA="$JAVA_HOME/bin/java"
elif command -v java >/dev/null 2>&1 && java -version >/dev/null 2>&1; then
    JAVA="java"
elif [[ -x /usr/libexec/java_home ]] && /usr/libexec/java_home >/dev/null 2>&1; then
    JAVA="$(/usr/libexec/java_home)/bin/java"
else
    echo "Error: no Java runtime found. Run ./setup-jlox.sh for details" >&2
    exit 1
fi

exec "$JAVA" -jar "$JLOX_JAR" "$@"
