import pytest

from plox.ast_printer import AstPrinter
from plox.parser import Parser, ParserError
from plox.scanner import Scanner


@pytest.fixture
def parse():
    """Return a helper that scans then parses `source` into an Expr.

    Driving the parser through the real Scanner mirrors how it is used in
    practice and keeps expectations free of token-construction details.
    """

    def _parse(source):
        return Parser(Scanner(source).scan()).parse()

    return _parse


@pytest.fixture
def show(parse):
    """Return a helper that parses `source` to its S-expression string.

    The AstPrinter serialization captures the shape of the tree -- operator
    precedence, associativity, and grouping -- in a compact, position-
    independent form, so expectations read as the parse the grammar dictates.
    """

    def _show(source):
        return AstPrinter().print(parse(source))

    return _show


@pytest.mark.parametrize(
    "source, expected",
    [
        ("123", "123"),
        ("0", "0"),
        ("123.456", "123.456"),
        ('"hello"', "hello"),
        ('""', ""),
        ('"a b c"', "a b c"),
        ("true", "true"),
        ("false", "false"),
        ("nil", "nil"),
    ],
)
def test_literals(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("(123)", "(grp 123)"),
        ("(true)", "(grp true)"),
        ('("hi")', "(grp hi)"),
        ("((1))", "(grp (grp 1))"),
        # a grouping is transparent to precedence of what it wraps
        ("(1 + 2)", "(grp (+ 1 2))"),
    ],
)
def test_grouping(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("-1", "(- 1)"),
        ("!true", "(! true)"),
        ("!false", "(! false)"),
        # unary is right-recursive, so it stacks
        ("--1", "(- (- 1))"),
        ("!!false", "(! (! false))"),
        ("!-1", "(! (- 1))"),
        ("-(1)", "(- (grp 1))"),
    ],
)
def test_unary(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 * 2", "(* 1 2)"),
        ("6 / 3", "(/ 6 3)"),
        # factor is left-associative
        ("1 * 2 * 3", "(* (* 1 2) 3)"),
        ("8 / 4 / 2", "(/ (/ 8 4) 2)"),
        ("1 * 2 / 3", "(/ (* 1 2) 3)"),
        # unary binds tighter than factor
        ("-1 * 2", "(* (- 1) 2)"),
        ("1 * -2", "(* 1 (- 2))"),
    ],
)
def test_factor(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 + 2", "(+ 1 2)"),
        ("3 - 1", "(- 3 1)"),
        # term is left-associative
        ("1 + 2 + 3", "(+ (+ 1 2) 3)"),
        ("1 - 2 - 3", "(- (- 1 2) 3)"),
        ("1 + 2 - 3", "(- (+ 1 2) 3)"),
        # factor binds tighter than term
        ("1 + 2 * 3", "(+ 1 (* 2 3))"),
        ("1 * 2 + 3", "(+ (* 1 2) 3)"),
        ("1 - 2 / 3", "(- 1 (/ 2 3))"),
    ],
)
def test_term(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 < 2", "(< 1 2)"),
        ("1 <= 2", "(<= 1 2)"),
        ("1 > 2", "(> 1 2)"),
        ("1 >= 2", "(>= 1 2)"),
        # comparison is left-associative
        ("1 < 2 < 3", "(< (< 1 2) 3)"),
        # term binds tighter than comparison
        ("1 + 2 < 3", "(< (+ 1 2) 3)"),
        ("1 < 2 + 3", "(< 1 (+ 2 3))"),
    ],
)
def test_comparison(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 == 2", "(== 1 2)"),
        ("1 != 2", "(!= 1 2)"),
        ("true == false", "(== true false)"),
        ("nil != nil", "(!= nil nil)"),
        # equality is left-associative
        ("1 == 2 == 3", "(== (== 1 2) 3)"),
        ("1 != 2 != 3", "(!= (!= 1 2) 3)"),
        # comparison binds tighter than equality
        ("1 < 2 == 3 > 4", "(== (< 1 2) (> 3 4))"),
    ],
)
def test_equality(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the full precedence ladder in one expression
        ("!true == 1 + 2 * 3 < 4", "(== (! true) (< (+ 1 (* 2 3)) 4))"),
        # grouping overrides the default precedence
        ("(1 + 2) * 3", "(* (grp (+ 1 2)) 3)"),
        ("2 * (3 + 4)", "(* 2 (grp (+ 3 4)))"),
        ("-(1 + 2)", "(- (grp (+ 1 2)))"),
        ("!(1 == 2)", "(! (grp (== 1 2)))"),
    ],
)
def test_precedence_and_grouping(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a grouping wrapping either branch of a binary node
        ("(1 * 2) + (3 * 4)", "(+ (grp (* 1 2)) (grp (* 3 4)))"),
        ("((1 + 2) * (3 - 4))", "(grp (* (grp (+ 1 2)) (grp (- 3 4))))"),
        # groupings nested to arbitrary depth
        ("((((1))))", "(grp (grp (grp (grp 1))))"),
        ("(1 + (2 * (3 - 4)))", "(grp (+ 1 (grp (* 2 (grp (- 3 4))))))"),
        # asymmetric branching: groupings sit at different depths per side
        (
            "((1 - 2) - (3 - (4 - 5)))",
            "(grp (- (grp (- 1 2)) (grp (- 3 (grp (- 4 5))))))",
        ),
        (
            "((1 + 2) * ((3 - 4) / 5))",
            "(grp (* (grp (+ 1 2)) (grp (/ (grp (- 3 4)) 5))))",
        ),
    ],
)
def test_nested_grouping(show, source, expected):
    assert show(source) == expected


@pytest.mark.parametrize(
    "source, position",
    [
        # nothing to parse at all
        ("", (1, 1)),
        # a binary operator with no left operand
        ("* 3", (1, 1)),
        ("/ 3", (1, 1)),
        ("< 3", (1, 1)),
        ("== 3", (1, 1)),
        # a stray closing paren is not the start of an expression
        (")", (1, 1)),
        # identifiers are not yet valid primary expressions
        ("foo", (1, 1)),
        # keywords that do not begin an expression
        ("var", (1, 1)),
        ("if", (1, 1)),
        # a binary operator with no right operand
        ("1 +", (1, 4)),
        ("1 -", (1, 4)),
        ("1 *", (1, 4)),
        ("1 <", (1, 4)),
        ("1 ==", (1, 5)),
        # a unary operator with no operand
        ("-", (1, 2)),
        ("!", (1, 2)),
        # an operator where an operand was expected, mid-expression
        ("1 + * 2", (1, 5)),
        # an empty grouping fails on the missing inner expression
        ("()", (1, 2)),
    ],
)
def test_expected_expression_error(parse, source, position):
    with pytest.raises(ParserError) as excinfo:
        parse(source)
    assert str(excinfo.value) == "Expected expression"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("(1 + 2", (1, 7)),
        ("(1", (1, 3)),
        ("((1)", (1, 5)),
        # a stray token where the closing paren was expected
        ("(1 + 2 3", (1, 8)),
        ("(1 2)", (1, 4)),
    ],
)
def test_missing_closing_paren_error(parse, source, position):
    with pytest.raises(ParserError) as excinfo:
        parse(source)
    assert str(excinfo.value) == "Expected ')' after expression"
    assert excinfo.value.get_line_info() == position
