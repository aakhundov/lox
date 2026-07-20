import pytest

from plox.ast import Expression
from plox.ast_printer import AstPrinter
from plox.parser import Parser, ParserError
from plox.scanner import Scanner


@pytest.fixture
def parse():
    """Return a helper that scans then parses `source` into a list of Stmt.

    Driving the parser through the real Scanner mirrors how it is used in
    practice and keeps expectations free of token-construction details.
    """

    def _parse(source):
        return Parser(Scanner(source).scan()).parse()

    return _parse


@pytest.fixture
def show_expr(parse):
    """Return a helper that renders `source`'s expression as an S-expression.

    The expression-grammar tests care about precedence, associativity, and
    grouping -- not the enclosing statement -- so `source` is a single
    expression statement and the helper renders just its inner expression.
    """

    def _show(source):
        statements = parse(source)
        assert len(statements) == 1
        stmt = statements[0]
        assert isinstance(stmt, Expression)
        return AstPrinter().print(stmt.expression)

    return _show


@pytest.fixture
def show_one(parse):
    """Return a helper that parses a single statement and renders it."""

    def _show(source):
        statements = parse(source)
        assert len(statements) == 1
        return AstPrinter().print(statements[0])

    return _show


@pytest.fixture
def show_all(parse):
    """Return a helper that renders every parsed statement as an S-expression."""

    def _show(source):
        return [AstPrinter().print(s) for s in parse(source)]

    return _show


@pytest.mark.parametrize(
    "source, expected",
    [
        ("123;", "123"),
        ("0;", "0"),
        ("123.456;", "123.456"),
        ('"hello";', '"hello"'),
        ('"";', '""'),
        ('"a b c";', '"a b c"'),
        ("true;", "true"),
        ("false;", "false"),
        ("nil;", "nil"),
    ],
)
def test_literals(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("(123);", "(grp 123)"),
        ("(true);", "(grp true)"),
        ('("hi");', '(grp "hi")'),
        ("((1));", "(grp (grp 1))"),
        # a grouping is transparent to precedence of what it wraps
        ("(1 + 2);", "(grp (+ 1 2))"),
    ],
)
def test_grouping(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("-1;", "(- 1)"),
        ("!true;", "(! true)"),
        ("!false;", "(! false)"),
        # unary is right-recursive, so it stacks
        ("--1;", "(- (- 1))"),
        ("!!false;", "(! (! false))"),
        ("!-1;", "(! (- 1))"),
        ("-(1);", "(- (grp 1))"),
    ],
)
def test_unary(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 * 2;", "(* 1 2)"),
        ("6 / 3;", "(/ 6 3)"),
        # factor is left-associative
        ("1 * 2 * 3;", "(* (* 1 2) 3)"),
        ("8 / 4 / 2;", "(/ (/ 8 4) 2)"),
        ("1 * 2 / 3;", "(/ (* 1 2) 3)"),
        # unary binds tighter than factor
        ("-1 * 2;", "(* (- 1) 2)"),
        ("1 * -2;", "(* 1 (- 2))"),
    ],
)
def test_factor(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 + 2;", "(+ 1 2)"),
        ("3 - 1;", "(- 3 1)"),
        # term is left-associative
        ("1 + 2 + 3;", "(+ (+ 1 2) 3)"),
        ("1 - 2 - 3;", "(- (- 1 2) 3)"),
        ("1 + 2 - 3;", "(- (+ 1 2) 3)"),
        # factor binds tighter than term
        ("1 + 2 * 3;", "(+ 1 (* 2 3))"),
        ("1 * 2 + 3;", "(+ (* 1 2) 3)"),
        ("1 - 2 / 3;", "(- 1 (/ 2 3))"),
    ],
)
def test_term(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 < 2;", "(< 1 2)"),
        ("1 <= 2;", "(<= 1 2)"),
        ("1 > 2;", "(> 1 2)"),
        ("1 >= 2;", "(>= 1 2)"),
        # comparison is left-associative
        ("1 < 2 < 3;", "(< (< 1 2) 3)"),
        # term binds tighter than comparison
        ("1 + 2 < 3;", "(< (+ 1 2) 3)"),
        ("1 < 2 + 3;", "(< 1 (+ 2 3))"),
    ],
)
def test_comparison(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 == 2;", "(== 1 2)"),
        ("1 != 2;", "(!= 1 2)"),
        ("true == false;", "(== true false)"),
        ("nil != nil;", "(!= nil nil)"),
        # equality is left-associative
        ("1 == 2 == 3;", "(== (== 1 2) 3)"),
        ("1 != 2 != 3;", "(!= (!= 1 2) 3)"),
        # comparison binds tighter than equality
        ("1 < 2 == 3 > 4;", "(== (< 1 2) (> 3 4))"),
    ],
)
def test_equality(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the full precedence ladder in one expression
        ("!true == 1 + 2 * 3 < 4;", "(== (! true) (< (+ 1 (* 2 3)) 4))"),
        # grouping overrides the default precedence
        ("(1 + 2) * 3;", "(* (grp (+ 1 2)) 3)"),
        ("2 * (3 + 4);", "(* 2 (grp (+ 3 4)))"),
        ("-(1 + 2);", "(- (grp (+ 1 2)))"),
        ("!(1 == 2);", "(! (grp (== 1 2)))"),
    ],
)
def test_precedence_and_grouping(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a grouping wrapping either branch of a binary node
        ("(1 * 2) + (3 * 4);", "(+ (grp (* 1 2)) (grp (* 3 4)))"),
        ("((1 + 2) * (3 - 4));", "(grp (* (grp (+ 1 2)) (grp (- 3 4))))"),
        # groupings nested to arbitrary depth
        ("((((1))));", "(grp (grp (grp (grp 1))))"),
        ("(1 + (2 * (3 - 4)));", "(grp (+ 1 (grp (* 2 (grp (- 3 4))))))"),
        # asymmetric branching: groupings sit at different depths per side
        (
            "((1 - 2) - (3 - (4 - 5)));",
            "(grp (- (grp (- 1 2)) (grp (- 3 (grp (- 4 5))))))",
        ),
        (
            "((1 + 2) * ((3 - 4) / 5));",
            "(grp (* (grp (+ 1 2)) (grp (/ (grp (- 3 4)) 5))))",
        ),
    ],
)
def test_nested_grouping(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 + 2;", "(expr (+ 1 2))"),
        ('"hi";', '(expr "hi")'),
        ("nil;", "(expr nil)"),
        # the statement is transparent to the expression it wraps
        ("(1);", "(expr (grp 1))"),
    ],
)
def test_expression_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("print 1;", "(print 1)"),
        ("print 1 + 2;", "(print (+ 1 2))"),
        ('print "a";', '(print "a")'),
        ("print nil;", "(print nil)"),
    ],
)
def test_print_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # varargs: multiple comma-separated expressions on one print
        ("print 1, 2, 3;", "(print 1 2 3)"),
        ('print "a", "b";', '(print "a" "b")'),
        # each argument is a full expression
        ("print 1 + 2, 3 * 4;", "(print (+ 1 2) (* 3 4))"),
    ],
)
def test_print_varargs(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # multiple statements parse in source order
        ("1; 2;", ["(expr 1)", "(expr 2)"]),
        (
            "print 1; 2 + 3; print 4;",
            ["(print 1)", "(expr (+ 2 3))", "(print 4)"],
        ),
        # empty source is legal and yields no statements
        ("", []),
    ],
)
def test_statement_sequence(show_all, source, expected):
    assert show_all(source) == expected


@pytest.mark.parametrize(
    "source, position",
    [
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
        # print with no expression to print
        ("print;", (1, 6)),
        # a trailing comma leaves print without its next value
        ("print 1,;", (1, 9)),
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


@pytest.mark.parametrize(
    "source, message, position",
    [
        # an expression statement without its terminating ';'
        ("1 + 2", "Expect ';' after expression", (1, 6)),
        # a trailing token where ';' was expected ends the statement
        ("1 2", "Expect ';' after expression", (1, 3)),
        # a print statement without its terminating ';'
        ("print 1", "Expect ';' after values", (1, 8)),
        ("print 1 2", "Expect ';' after values", (1, 9)),
    ],
)
def test_missing_semicolon_error(parse, source, message, position):
    with pytest.raises(ParserError) as excinfo:
        parse(source)
    assert str(excinfo.value) == message
    assert excinfo.value.get_line_info() == position
