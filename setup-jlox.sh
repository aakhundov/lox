#!/bin/bash
#
# One-time setup for running jlox. Compiles the interpreter and packages it
# into a runnable jar, so the ./run-jlox.sh launcher starts without
# recompiling every time.
#
# Safe to re-run: it rebuilds from scratch.
#
# Override the JDK used to build with, e.g.:
#   JAVA_HOME=/path/to/jdk ./setup-jlox.sh

set -euo pipefail

# Resolve the repo root from this script's location so it works from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JLOX_DIR="$SCRIPT_DIR/jlox"
SRC_DIR="$JLOX_DIR/src/main/java"
BUILD_DIR="$JLOX_DIR/build"
CLASSES_DIR="$BUILD_DIR/classes"
JAR="$BUILD_DIR/jlox.jar"

# Prefer an explicit JAVA_HOME, then the tools on PATH. On macOS the
# /usr/bin/javac stub can fail to locate a JDK, so fall back to java_home.
if [[ -n "${JAVA_HOME:-}" ]]; then
    JAVAC="$JAVA_HOME/bin/javac"
    JAR_TOOL="$JAVA_HOME/bin/jar"
elif command -v javac >/dev/null 2>&1 && javac -version >/dev/null 2>&1; then
    JAVAC="javac"
    JAR_TOOL="jar"
elif [[ -x /usr/libexec/java_home ]] && /usr/libexec/java_home >/dev/null 2>&1; then
    DETECTED="$(/usr/libexec/java_home)"
    JAVAC="$DETECTED/bin/javac"
    JAR_TOOL="$DETECTED/bin/jar"
else
    echo "Error: no JDK found. Install one (e.g. 'brew install openjdk')" >&2
    echo "or point JAVA_HOME at it: JAVA_HOME=/path/to/jdk $0" >&2
    exit 1
fi

if [[ ! -x "$JAVAC" ]] && ! command -v "$JAVAC" >/dev/null 2>&1; then
    echo "Error: '$JAVAC' is not executable. Check your JAVA_HOME" >&2
    exit 1
fi

echo "Building jlox with $("$JAVAC" -version 2>&1) ..."
rm -rf "$BUILD_DIR"
mkdir -p "$CLASSES_DIR"

# jlox has no dependencies, so a plain javac over the source tree is enough.
find "$SRC_DIR" -name '*.java' -print0 | xargs -0 "$JAVAC" -d "$CLASSES_DIR"

echo "Packaging $JAR ..."
"$JAR_TOOL" --create --file "$JAR" --main-class jlox.Lox -C "$CLASSES_DIR" .

echo
echo "Done. Start jlox with:"
echo "  ./run-jlox.sh              # interactive REPL"
echo "  ./run-jlox.sh program.lox  # run a file"
echo
echo "To regenerate Expr.java and Stmt.java from the AST definitions:"
echo "  java -cp $JAR jlox.tool.GenerateAst $SRC_DIR/jlox"
