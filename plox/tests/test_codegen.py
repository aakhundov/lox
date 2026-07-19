import importlib.util
import sys
from pathlib import Path

import pytest

_PROJECT_ROOT = Path(__file__).resolve().parents[1]
_GENERATOR = _PROJECT_ROOT / "tools" / "generate_ast.py"
_COMMITTED_EXPR = _PROJECT_ROOT / "plox" / "ast" / "expr.py"


def _load_generator():
    # the generator lives outside the package, so load it by path
    spec = importlib.util.spec_from_file_location("generate_ast", _GENERATOR)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


generate_ast = _load_generator()


def test_committed_expr_matches_generator(tmp_path):
    """The committed expr.py is byte-identical to a fresh regeneration."""
    if not Path(sys.executable).with_name("ruff").exists():
        pytest.skip("ruff not installed; needed to format the generated file")

    generate_ast._make_file("Expr", tmp_path)
    regenerated = (tmp_path / "expr.py").read_text(encoding="utf-8")
    assert regenerated == _COMMITTED_EXPR.read_text(encoding="utf-8")
