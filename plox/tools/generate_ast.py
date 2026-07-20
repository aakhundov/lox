import subprocess
import sys

from pathlib import Path


_METADATA = {
    "Expr": {
        "Grouping": {"expression": "Expr"},
        "Binary": {"left": "Expr", "operator": "Token", "right": "Expr"},
        "Unary": {"operator": "Token", "right": "Expr"},
        "Literal": {"value": "LoxValue"},
    }
}

_HEADER = """\
# THIS FILE IS AUTO-GENERATED: DON'T EDIT BY HAND
# to-regenerate: `python plox/tools/generate_ast.py`

from abc import ABC, abstractmethod
from dataclasses import dataclass

from plox.common import Token, LoxValue
"""


def _get_ast_root() -> Path:
    this_path = Path(__file__).resolve()
    plox_path = this_path.parents[1]
    ast_root = plox_path / "plox/ast"

    if not (ast_root.exists() and ast_root.is_dir()):
        print(
            f"The path {ast_root} doesn't exist or isn't a dir",
            file=sys.stderr,
        )
        sys.exit(66)  # EX_NOINPUT

    return ast_root


def _generate_code(cls: str) -> str:
    lines = [_HEADER]

    def add(indent: int, line: str) -> None:
        lines.append(f"{' ' * indent * 4}{line}")

    # parent class and nested visitor (abstract)
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


def _make_file(cls: str, ast_root: Path) -> None:
    code = _generate_code(cls)

    # write to file
    path = ast_root / f"{cls.lower()}.py"
    path.write_text(code, encoding="utf-8")

    # format the file with ruff
    ruff = Path(sys.executable).with_name("ruff")
    subprocess.run([str(ruff), "format", str(path)], check=True)


def main() -> None:
    ast_root = _get_ast_root()
    for cls in _METADATA:
        print(f"Generating {cls}... ", end="")
        _make_file(cls, ast_root)
        print("DONE")


if __name__ == "__main__":
    main()
