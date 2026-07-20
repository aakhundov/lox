import pytest

from plox.interpreter import Interpreter, InterpreterError
from plox.parser import Parser
from plox.scanner import Scanner


@pytest.fixture
def evaluate():
    """Return a helper that scans, parses, then evaluates `source`.

    Driving the interpreter through the real Scanner and Parser mirrors how
    it is used in practice and keeps expectations free of AST-construction
    details -- each case is written as the Lox source a user would type.
    """

    def _evaluate(source):
        return Interpreter().interpret(Parser(Scanner(source).scan()).parse())

    return _evaluate


def _assert_value(result, expected):
    """Assert a Lox value equals `expected`, exact runtime type included.

    Lox numbers are Python floats, booleans are bools, nil is None, and
    strings are str. Comparing the type as well as the value keeps these
    distinct -- otherwise Python's `True == 1.0` / `False == 0.0` would let a
    number masquerade as a boolean (and vice versa) and hide real bugs.
    """

    assert type(result) is type(expected)
    assert result == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("123", 123.0),
        ("0", 0.0),
        ("123.456", 123.456),
        ('"hello"', "hello"),
        ('""', ""),
        ('"a b c"', "a b c"),
        ("true", True),
        ("false", False),
        ("nil", None),
    ],
)
def test_literals(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # a grouping evaluates to whatever it wraps
        ("(123)", 123.0),
        ("(true)", True),
        ('("hi")', "hi"),
        ("((5))", 5.0),
        ("(1 + 2)", 3.0),
        ("2 * (3 + 4)", 14.0),
    ],
)
def test_grouping(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("-5", -5.0),
        ("-0", 0.0),
        ("-123.5", -123.5),
        # unary minus stacks
        ("- -5", 5.0),
        ("--5", 5.0),
        ("-(1 + 2)", -3.0),
    ],
)
def test_unary_negation(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # only false and nil, plus Python-like empty/zero values, are falsy
        ("!true", False),
        ("!false", True),
        ("!nil", True),
        ("!0", True),
        ("!1", False),
        ("!123.5", False),
        ('!""', True),
        ('!"a"', False),
        # `!` stacks and reflects truthiness back to a bool
        ("!!true", True),
        ("!!nil", False),
        ("!-1", False),
    ],
)
def test_logical_not(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 + 2", 3.0),
        ("5 - 3", 2.0),
        ("4 * 2", 8.0),
        ("7 / 2", 3.5),
        ("6 / 3", 2.0),
        ("1.5 + 2.5", 4.0),
        ("10 - 20", -10.0),
    ],
)
def test_arithmetic(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ('"foo" + "bar"', "foobar"),
        ('"a" + ""', "a"),
        ('"" + "b"', "b"),
        ('"a" + "b" + "c"', "abc"),
    ],
)
def test_string_concatenation(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 < 2", True),
        ("2 < 1", False),
        ("1 <= 1", True),
        ("2 <= 1", False),
        ("3 > 2", True),
        ("2 > 3", False),
        ("2 >= 2", True),
        ("2 >= 3", False),
    ],
)
def test_comparison(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # strings order lexicographically by Unicode code point
        ('"a" < "b"', True),
        ('"b" < "a"', False),
        ('"a" <= "a"', True),
        ('"b" >= "b"', True),
        ('"apple" < "banana"', True),
        ('"apple" > "banana"', False),
        # a prefix sorts before the longer string
        ('"ab" < "abc"', True),
        ('"abc" <= "ab"', False),
        # uppercase sorts before lowercase (ASCII order)
        ('"Z" < "a"', True),
        # equal strings are neither strictly less nor greater
        ('"a" < "a"', False),
        ('"a" > "a"', False),
    ],
)
def test_string_comparison(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # same-type equality compares by value
        ("1 == 1", True),
        ("1 == 2", False),
        ("1 != 2", True),
        ('"a" == "a"', True),
        ('"a" == "b"', False),
        ("true == true", True),
        ("true == false", False),
        ("false == false", True),
        ("nil == nil", True),
        ("nil != nil", False),
    ],
)
def test_equality_same_type(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # values of different types are never equal, even across the
        # bool/number boundary where Python would conflate them
        ("1 == true", False),
        ("0 == false", False),
        ("1 != true", True),
        ("0 != false", True),
        ('1 == "1"', False),
        ('"true" == true', False),
        ("nil == false", False),
        ("nil == 0", False),
        ("nil != 0", True),
    ],
)
def test_equality_cross_type(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # factor binds tighter than term
        ("1 + 2 * 3", 7.0),
        ("2 * 3 + 4 * 5", 26.0),
        # grouping overrides precedence
        ("(1 + 2) * 3", 9.0),
        # unary binds tighter than factor
        ("-2 * 3", -6.0),
        # term is left-associative
        ("1 - 2 - 3", -4.0),
        ("8 / 4 / 2", 1.0),
        # comparison and term
        ("1 + 2 < 4", True),
        ("1 + 2 < 3", False),
        # equality sits below comparison, which sits below arithmetic
        ("1 < 2 == true", True),
        ("!true == false", True),
        ("2 * 3 == 6", True),
    ],
)
def test_precedence(evaluate, source, expected):
    _assert_value(evaluate(source), expected)


@pytest.mark.parametrize(
    "source, position",
    [
        # `+` requires both operands to be numbers or both strings
        ('1 + "a"', (1, 3)),
        ('"a" + 1', (1, 5)),
        ("true + 1", (1, 6)),
        ("1 + true", (1, 3)),
        ("nil + 2", (1, 5)),
    ],
)
def test_plus_operand_error(evaluate, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        evaluate(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, message, position",
    [
        # -, *, / require numeric operands; the message names the bad side
        ('1 - "a"', "Right operand must be a number", (1, 3)),
        ('"a" * 2', "Left operand must be a number", (1, 5)),
        ('2 / "b"', "Right operand must be a number", (1, 3)),
        ("true - 1", "Left operand must be a number", (1, 6)),
        ("nil * 2", "Left operand must be a number", (1, 5)),
    ],
)
def test_arithmetic_operand_error(evaluate, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        evaluate(source)
    assert str(excinfo.value) == message
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        # relational operators need both operands numbers or both strings;
        # mixed or otherwise-typed operands are an error
        ('1 < "a"', (1, 3)),
        ('"a" > 1', (1, 5)),
        ("true <= 1", (1, 6)),
        ("nil >= 1", (1, 5)),
        # same non-numeric/non-string type on both sides is still an error
        ("true < false", (1, 6)),
        ("nil > nil", (1, 5)),
    ],
)
def test_comparison_operand_error(evaluate, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        evaluate(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("1 / 0", (1, 3)),
        ("5 / 0", (1, 3)),
        ("10 / 0", (1, 4)),
        ("1 / (2 - 2)", (1, 3)),
    ],
)
def test_division_by_zero_error(evaluate, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        evaluate(source)
    assert str(excinfo.value) == "Division by zero"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        # unary minus requires a number
        ("-true", (1, 1)),
        ("-false", (1, 1)),
        ("-nil", (1, 1)),
        ('-"a"', (1, 1)),
    ],
)
def test_unary_negation_error(evaluate, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        evaluate(source)
    assert str(excinfo.value) == "Operand must be a number"
    assert excinfo.value.get_line_info() == position
