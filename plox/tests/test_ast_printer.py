import pytest

from plox.ast.expr import Binary, Grouping, Literal, Unary
from plox.ast_printer import AstPrinter
from plox.common import Token, TokenType as TT


def op(lexeme, token_type=TT.MINUS):
    """Build an operator token; only the lexeme affects the printer."""
    return Token(token_type, lexeme, None, 0)


@pytest.fixture
def show():
    """Return a helper that prints an Expr to its S-expression string."""
    return AstPrinter().print


@pytest.mark.parametrize(
    "value, expected",
    [
        (123.0, "123"),
        (45.67, "45.67"),
        ("hello", "hello"),
        (True, "true"),
        (False, "false"),
        (None, "nil"),
    ],
)
def test_literal(show, value, expected):
    assert show(Literal(value)) == expected


def test_unary(show):
    assert show(Unary(op("-", TT.MINUS), Literal(123.0))) == "(- 123)"
    assert show(Unary(op("!", TT.BANG), Literal(True))) == "(! true)"


def test_binary(show):
    assert show(Binary(Literal(1.0), op("+", TT.PLUS), Literal(2.5))) == "(+ 1 2.5)"
    assert show(Binary(Literal(6.3), op("*", TT.STAR), Literal(7.0))) == "(* 6.3 7)"


def test_grouping(show):
    assert show(Grouping(Literal(45.67))) == "(grouping 45.67)"
    assert show(Grouping(Literal("hi"))) == "(grouping hi)"


def test_nested(show):
    # -123 * (45.67)  -->  (* (- 123) (grouping 45.67))
    expr = Binary(
        Unary(op("-", TT.MINUS), Literal(123.0)),
        op("*", TT.STAR),
        Grouping(Literal(45.67)),
    )
    assert show(expr) == "(* (- 123) (grouping 45.67))"
