import pytest

from plox.ast import (
    Binary,
    Grouping,
    Literal,
    Unary,
    Variable,
    Assign,
    Print,
    Var,
    Block,
)
from plox.ast_printer import AstPrinter
from plox.common import Token, TokenType as TT


def op(lexeme, token_type=TT.MINUS):
    """Build an operator token; only the lexeme affects the printer."""
    return Token(token_type, lexeme, None, 0)


def ident(lexeme):
    """Build an identifier token; only the lexeme affects the printer."""
    return Token(TT.IDENTIFIER, lexeme, None, 0)


@pytest.fixture
def show():
    """Return a helper that prints an Expr to its S-expression string."""
    return AstPrinter().print


@pytest.mark.parametrize(
    "value, expected",
    [
        (123.0, "123"),
        (45.67, "45.67"),
        ("hello", '"hello"'),
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
    assert show(Grouping(Literal(45.67))) == "(grp 45.67)"
    assert show(Grouping(Literal("hi"))) == '(grp "hi")'


def test_nested(show):
    # -123 * (45.67)  -->  (* (- 123) (grp 45.67))
    expr = Binary(
        Unary(op("-", TT.MINUS), Literal(123.0)),
        op("*", TT.STAR),
        Grouping(Literal(45.67)),
    )
    assert show(expr) == "(* (- 123) (grp 45.67))"


def test_variable(show):
    assert show(Variable(ident("x"))) == "x"
    assert show(Variable(ident("foo"))) == "foo"


def test_assign(show):
    assert show(Assign(ident("x"), Literal(1.0))) == "(= x 1)"
    expr = Assign(ident("x"), Binary(Literal(1.0), op("+", TT.PLUS), Literal(2.0)))
    assert show(expr) == "(= x (+ 1 2))"


def test_var_statement(show):
    # a declaration with no initializer omits the value
    assert show(Var(ident("x"), None)) == "(var x)"
    assert show(Var(ident("x"), Literal(1.0))) == "(var x 1)"


def test_block(show):
    # an empty block has no children
    assert show(Block([])) == "(blk)"
    assert show(Block([Print([Literal(1.0)])])) == "(blk (print 1))"
    # blocks nest
    assert show(Block([Block([])])) == "(blk (blk))"
