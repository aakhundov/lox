import pytest

from plox.ast import (
    Conditional,
    Logical,
    Binary,
    Grouping,
    Literal,
    Unary,
    Call,
    Get,
    Super,
    This,
    Variable,
    Assign,
    Set,
    Class,
    Function,
    For,
    If,
    While,
    LoopJump,
    Print,
    Return,
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
        # literals render as the Lox source they came from, so a small number
        # keeps its positional form rather than Python's `1e-07`
        (0.0000001, "0.0000001"),
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


def test_super(show):
    # the keyword and the method name after the dot are one unit of syntax --
    # `super` is never an expression on its own -- so they render together
    # rather than as an object with a property hung off it
    keyword = Token(TT.SUPER, "super", None, 0)
    assert show(Super(keyword, ident("m"))) == "super.m"
    assert show(Super(keyword, ident("init"))) == "super.init"
    # so a super call wraps the pair whole, where `this.m()` nests a `get`
    node = Call(Super(keyword, ident("m")), op(")", TT.RIGHT_PAREN), [Literal(1.0)])
    assert show(node) == "(call super.m 1)"


def test_this(show):
    # `this` renders as its bare keyword, like a variable does
    assert show(This(Token(TT.THIS, "this", None, 0))) == "this"


def test_assign(show):
    assert show(Assign(ident("x"), Literal(1.0))) == "(= x 1)"
    expr = Assign(ident("x"), Binary(Literal(1.0), op("+", TT.PLUS), Literal(2.0)))
    assert show(expr) == "(= x (+ 1 2))"


def test_set(show):
    # the property name sits in the head; the object and the value are children
    node = Set(Variable(ident("obj")), ident("x"), Literal(1.0))
    assert show(node) == "(set x obj 1)"
    # the object is an arbitrary expression, so a chained target nests
    nested = Set(Get(Variable(ident("a")), ident("b")), ident("c"), Literal(2.0))
    assert show(nested) == "(set c (get b a) 2)"
    # so is the value
    node = Set(
        This(Token(TT.THIS, "this", None, 0)),
        ident("x"),
        Binary(Literal(1.0), op("+", TT.PLUS), Literal(2.0)),
    )
    assert show(node) == "(set x this (+ 1 2))"


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


def test_call(show):
    callee = Variable(ident("f"))
    paren = op(")", TT.RIGHT_PAREN)
    # a call with no arguments has the callee as its only child
    assert show(Call(callee, paren, [])) == "(call f)"
    assert show(Call(callee, paren, [Literal(1.0)])) == "(call f 1)"
    assert show(Call(callee, paren, [Literal(1.0), Literal("x")])) == '(call f 1 "x")'
    # the callee is an arbitrary expression, so calls nest
    inner = Call(callee, paren, [Literal(1.0)])
    assert show(Call(inner, paren, [Literal(2.0)])) == "(call (call f 1) 2)"


def test_get(show):
    # the property name sits in the head, the object is the only child
    assert show(Get(Variable(ident("obj")), ident("x"))) == "(get x obj)"
    # `.` is left-associative, so a chain nests to the left
    chain = Get(Get(Variable(ident("a")), ident("b")), ident("c"))
    assert show(chain) == "(get c (get b a))"
    # the object is an arbitrary expression: a property of a call's result
    call = Call(Variable(ident("f")), op(")", TT.RIGHT_PAREN), [])
    assert show(Get(call, ident("x"))) == "(get x (call f))"


def test_class_statement(show):
    # a class with no methods has no children
    assert show(Class(ident("A"), None, [])) == "(class A)"
    # methods render as the function statements they are
    node = Class(ident("A"), None, [Function(ident("m"), [], [])])
    assert show(node) == "(class A (fun m ()))"
    # several methods render as successive children
    node = Class(
        ident("A"),
        None,
        [
            Function(ident("init"), [ident("a")], [Print([Variable(ident("a"))])]),
            Function(
                ident("m"), [], [Return(Token(TT.RETURN, "return", None, 0), None)]
            ),
        ],
    )
    assert show(node) == "(class A (fun init (a) (print a)) (fun m () (return)))"


def test_class_statement_with_superclass(show):
    # a superclass is named in the head, after the class's own name, so a
    # subclass declaration is distinguishable from a base one
    superclass = Variable(ident("A"))
    assert show(Class(ident("B"), superclass, [])) == "(class B A)"
    # the methods stay children, as they are without a superclass
    node = Class(ident("B"), superclass, [Function(ident("m"), [], [])])
    assert show(node) == "(class B A (fun m ()))"


def test_function_statement(show):
    # the parameter list renders inside the head, so a body-less function
    # still shows its (empty) signature
    assert show(Function(ident("f"), [], [])) == "(fun f ())"
    assert show(Function(ident("f"), [ident("a")], [])) == "(fun f (a))"
    # multiple parameters are comma-separated
    node = Function(
        ident("f"), [ident("a"), ident("b")], [Print([Variable(ident("a"))])]
    )
    assert show(node) == "(fun f (a, b) (print a))"
    # body statements render as successive children
    node = Function(ident("f"), [], [Print([Literal(1.0)]), Print([Literal(2.0)])])
    assert show(node) == "(fun f () (print 1) (print 2))"


def test_return_statement(show):
    keyword = Token(TT.RETURN, "return", None, 0)
    # a bare `return` omits the value; the keyword token is carried for error
    # reporting but does not affect the rendering
    assert show(Return(keyword, None)) == "(return)"
    assert show(Return(keyword, Literal(1.0))) == "(return 1)"
    expr = Binary(Variable(ident("a")), op("+", TT.PLUS), Literal(1.0))
    assert show(Return(keyword, expr)) == "(return (+ a 1))"
