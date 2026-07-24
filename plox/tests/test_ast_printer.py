import pytest

from plox.ast import (
    Conditional,
    Logical,
    Binary,
    Grouping,
    Literal,
    Unary,
    Variable,
    Assign,
    For,
    If,
    While,
    LoopJump,
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


def test_logical(show):
    left, right = Variable(ident("a")), Variable(ident("b"))
    assert show(Logical(left, op("and", TT.AND), right)) == "(and a b)"
    assert show(Logical(left, op("or", TT.OR), right)) == "(or a b)"


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


def test_conditional(show):
    node = Conditional(Variable(ident("a")), Literal(1.0), Literal(2.0))
    assert show(node) == "(?: a 1 2)"
    # the condition and branches are arbitrary expressions
    nested = Conditional(
        Binary(Literal(1.0), op("<", TT.LESS), Literal(2.0)),
        Literal("y"),
        Literal("n"),
    )
    assert show(nested) == '(?: (< 1 2) "y" "n")'


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


def test_if_statement(show):
    then_branch = Print([Literal(1.0)])
    # an `if` with no `else` omits the third child
    assert show(If(Literal(True), then_branch, None)) == "(if true (print 1))"
    assert (
        show(If(Literal(True), then_branch, Print([Literal(2.0)])))
        == "(if true (print 1) (print 2))"
    )


def test_while_statement(show):
    assert show(While(Literal(True), Print([Literal(1.0)]))) == "(while true (print 1))"


def test_for_statement(show):
    # a fully-specified loop renders initializer, condition, increment, body
    node = For(
        Var(ident("i"), Literal(0.0)),
        Binary(Variable(ident("i")), op("<", TT.LESS), Literal(3.0)),
        Assign(
            ident("i"),
            Binary(Variable(ident("i")), op("+", TT.PLUS), Literal(1.0)),
        ),
        Print([Variable(ident("i"))]),
    )
    assert show(node) == "(for (var i 0) (< i 3) (= i (+ i 1)) (print i))"
    # omitted clauses render as nil placeholders
    assert (
        show(For(None, None, None, Print([Literal(1.0)])))
        == "(for nil nil nil (print 1))"
    )


def test_loop_jump_statement(show):
    # a loop jump renders as its bare keyword
    assert show(LoopJump(Token(TT.BREAK, "break", None, 0))) == "(break)"
    assert show(LoopJump(Token(TT.CONTINUE, "continue", None, 0))) == "(continue)"
