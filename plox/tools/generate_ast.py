import subprocess
import sys

from pathlib import Path


_METADATA = {
    "Expr": {
        "Grouping": {"expression": "Expr"},
        "Binary": {"left": "Expr", "operator": "Token", "right": "Expr"},
        "Unary": {"operator": "Token", "right": "Expr"},
        "Literal": {"value": "LoxValue"},
        "Variable": {"name": "Token"},
        "Assign": {"name": "Token", "value": "Expr"},
    },
    "Stmt": {
        "Expression": {"expression": "Expr"},
        "Print": {"expressions": "list[Expr]"},
        "Var": {"name": "Token", "initializer": "Expr | None"},
        "Block": {"statements": "list[Stmt]"},
    },
}

_HEADER = """\
# THIS FILE IS AUTO-GENERATED: DON'T EDIT BY HAND
# to-regenerate: `python plox/tools/generate_ast.py`

from abc import ABC, abstractmethod
from dataclasses import dataclass

from plox.common import Token, LoxValue
"""


def _get_package_root() -> Path:
    this_path = Path(__file__).resolve()
    plox_path = this_path.parents[1]
    package_root = plox_path / "plox"

    if not (package_root.exists() and package_root.is_dir()):
        print(
            f"The path {package_root} doesn't exist or isn't a dir",
            file=sys.stderr,
        )
        sys.exit(66)  # EX_NOINPUT

    return package_root


def _generate_code(cls: str) -> str:
    lines = []

    def add(indent: int, line: str) -> None:
        lines.append(f"{' ' * indent * 4}{line}")

    # parent class + visitor (abstract)
    add(0, f"class {cls}(ABC):")
    add(1, "class Visitor[R](ABC):")
    for sub in _METADATA[cls]:
        add(2, "@abstractmethod")
        add(2, f'def visit_{sub.lower()}(self, {cls.lower()[0]}: "{sub}") -> R: ...')
    add(1, "@abstractmethod")
    add(1, "def accept[R](self, visitor: Visitor[R]) -> R: ...")

    # subclasses (concrete)
    for sub, fields in _METADATA[cls].items():
        add(0, "@dataclass(frozen=True)")
        add(0, f"class {sub}({cls}):")
        for name, type_ in fields.items():
            add(1, f"{name}: {type_}")
        add(1, f"def accept[R](self, visitor: {cls}.Visitor[R]) -> R:")
        add(2, f"return visitor.visit_{sub.lower()}(self)")

    return "\n".join(lines)


def _make_file(path: Path) -> None:
    sections = [_HEADER]
    for cls in _METADATA:
        print(f"Generating {cls}... ", end="")
        sections.append(_generate_code(cls))
        print("DONE")

    # write all code to the file
    code = "\n\n".join(sections)
    path.write_text(code, encoding="utf-8")

    # format the file with ruff
    ruff = Path(sys.executable).with_name("ruff")
    subprocess.run([str(ruff), "format", str(path)], check=True)


def main() -> None:
    root = _get_package_root()
    file = root / "ast.py"
    _make_file(file)


if __name__ == "__main__":
    main()
