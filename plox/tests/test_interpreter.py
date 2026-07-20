import pytest

from plox.common import LoxValue
from plox.interpreter import Interpreter, InterpreterError
from plox.parser import Parser
from plox.scanner import Scanner


@pytest.fixture
def run():
    """Return a helper that runs `source`, collecting the values it prints.

    The interpreter's `print_fn` seam is tapped with a raw collector, so each
    executed `print` appends its list of evaluated LoxValues (varargs are one
    group). Operating at the Python-implementation level lets tests observe the
    actual runtime values rather than their rendered text. A fresh interpreter
    runs each source, so no state leaks between cases.
    """

    def _run(source):
        collected: list[list[LoxValue]] = []
        Interpreter(print_fn=collected.append).interpret(
            Parser(Scanner(source).scan()).parse()
        )
        return collected

    return _run


@pytest.fixture
def value(run):
    """Return a helper that runs a single `print` of one expression.

    Value tests print exactly one expression; the helper unwraps the lone
    collected group and returns its single LoxValue for assertion.
    """

    def _value(source):
        collected = run(source)
        assert len(collected) == 1
        assert len(collected[0]) == 1
        return collected[0][0]

    return _value


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
        ("print 123;", 123.0),
        ("print 0;", 0.0),
        ("print 123.456;", 123.456),
        ('print "hello";', "hello"),
        ('print "";', ""),
        ('print "a b c";', "a b c"),
        ("print true;", True),
        ("print false;", False),
        ("print nil;", None),
    ],
)
def test_literals(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # a grouping evaluates to whatever it wraps
        ("print (123);", 123.0),
        ("print (true);", True),
        ('print ("hi");', "hi"),
        ("print ((5));", 5.0),
        ("print (1 + 2);", 3.0),
        ("print 2 * (3 + 4);", 14.0),
    ],
)
def test_grouping(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("print -5;", -5.0),
        ("print -0;", 0.0),
        ("print -123.5;", -123.5),
        # unary minus stacks
        ("print - -5;", 5.0),
        ("print --5;", 5.0),
        ("print -(1 + 2);", -3.0),
    ],
)
def test_unary_negation(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # only false and nil, plus Python-like empty/zero values, are falsy
        ("print !true;", False),
        ("print !false;", True),
        ("print !nil;", True),
        ("print !0;", True),
        ("print !1;", False),
        ("print !123.5;", False),
        ('print !"";', True),
        ('print !"a";', False),
        # `!` stacks and reflects truthiness back to a bool
        ("print !!true;", True),
        ("print !!nil;", False),
        ("print !-1;", False),
    ],
)
def test_logical_not(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("print 1 + 2;", 3.0),
        ("print 5 - 3;", 2.0),
        ("print 4 * 2;", 8.0),
        ("print 7 / 2;", 3.5),
        ("print 6 / 3;", 2.0),
        ("print 1.5 + 2.5;", 4.0),
        ("print 10 - 20;", -10.0),
    ],
)
def test_arithmetic(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ('print "foo" + "bar";', "foobar"),
        ('print "a" + "";', "a"),
        ('print "" + "b";', "b"),
        ('print "a" + "b" + "c";', "abc"),
    ],
)
def test_string_concatenation(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        ("print 1 < 2;", True),
        ("print 2 < 1;", False),
        ("print 1 <= 1;", True),
        ("print 2 <= 1;", False),
        ("print 3 > 2;", True),
        ("print 2 > 3;", False),
        ("print 2 >= 2;", True),
        ("print 2 >= 3;", False),
    ],
)
def test_comparison(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # strings order lexicographically by Unicode code point
        ('print "a" < "b";', True),
        ('print "b" < "a";', False),
        ('print "a" <= "a";', True),
        ('print "b" >= "b";', True),
        ('print "apple" < "banana";', True),
        ('print "apple" > "banana";', False),
        # a prefix sorts before the longer string
        ('print "ab" < "abc";', True),
        ('print "abc" <= "ab";', False),
        # uppercase sorts before lowercase (ASCII order)
        ('print "Z" < "a";', True),
        # equal strings are neither strictly less nor greater
        ('print "a" < "a";', False),
        ('print "a" > "a";', False),
    ],
)
def test_string_comparison(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # same-type equality compares by value
        ("print 1 == 1;", True),
        ("print 1 == 2;", False),
        ("print 1 != 2;", True),
        ('print "a" == "a";', True),
        ('print "a" == "b";', False),
        ("print true == true;", True),
        ("print true == false;", False),
        ("print false == false;", True),
        ("print nil == nil;", True),
        ("print nil != nil;", False),
    ],
)
def test_equality_same_type(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # values of different types are never equal, even across the
        # bool/number boundary where Python would conflate them
        ("print 1 == true;", False),
        ("print 0 == false;", False),
        ("print 1 != true;", True),
        ("print 0 != false;", True),
        ('print 1 == "1";', False),
        ('print "true" == true;', False),
        ("print nil == false;", False),
        ("print nil == 0;", False),
        ("print nil != 0;", True),
    ],
)
def test_equality_cross_type(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # factor binds tighter than term
        ("print 1 + 2 * 3;", 7.0),
        ("print 2 * 3 + 4 * 5;", 26.0),
        # grouping overrides precedence
        ("print (1 + 2) * 3;", 9.0),
        # unary binds tighter than factor
        ("print -2 * 3;", -6.0),
        # term is left-associative
        ("print 1 - 2 - 3;", -4.0),
        ("print 8 / 4 / 2;", 1.0),
        # comparison and term
        ("print 1 + 2 < 4;", True),
        ("print 1 + 2 < 3;", False),
        # equality sits below comparison, which sits below arithmetic
        ("print 1 < 2 == true;", True),
        ("print !true == false;", True),
        ("print 2 * 3 == 6;", True),
    ],
)
def test_precedence(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # each executed print appends one group of evaluated values
        ("print 1;", [[1.0]]),
        # varargs evaluate to one group, in order
        ("print 1, 2, 3;", [[1.0, 2.0, 3.0]]),
        ("print 1 + 1, 2 * 2;", [[2.0, 4.0]]),
        # successive prints append successive groups
        ("print 1; print 2;", [[1.0], [2.0]]),
        # expression statements evaluate but print nothing
        ("1 + 2;", []),
        ("1 + 2; print 3; 4 + 5;", [[3.0]]),
    ],
)
def test_print_grouping(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("print 1 + 2;", "3\n"),
        # strings are printed without quotes
        ('print "hello";', "hello\n"),
        ("print true;", "true\n"),
        ("print nil;", "nil\n"),
        # varargs render space-joined on a single line
        ("print 1, 2, 3;", "1 2 3\n"),
        ('print "a", 1, nil;', "a 1 nil\n"),
        # one line per executed print
        ("print 1; print 2;", "1\n2\n"),
    ],
)
def test_default_print_output(capsys, source, expected):
    Interpreter().interpret(Parser(Scanner(source).scan()).parse())
    assert capsys.readouterr().out == expected


@pytest.mark.parametrize(
    "source, position",
    [
        # `+` requires both operands to be numbers or both strings
        ('1 + "a";', (1, 3)),
        ('"a" + 1;', (1, 5)),
        ("true + 1;", (1, 6)),
        ("1 + true;", (1, 3)),
        ("nil + 2;", (1, 5)),
    ],
)
def test_plus_operand_error(run, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, message, position",
    [
        # -, *, / require numeric operands; the message names the bad side
        ('1 - "a";', "Right operand must be a number", (1, 3)),
        ('"a" * 2;', "Left operand must be a number", (1, 5)),
        ('2 / "b";', "Right operand must be a number", (1, 3)),
        ("true - 1;", "Left operand must be a number", (1, 6)),
        ("nil * 2;", "Left operand must be a number", (1, 5)),
    ],
)
def test_arithmetic_operand_error(run, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == message
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        # relational operators need both operands numbers or both strings;
        # mixed or otherwise-typed operands are an error
        ('1 < "a";', (1, 3)),
        ('"a" > 1;', (1, 5)),
        ("true <= 1;", (1, 6)),
        ("nil >= 1;", (1, 5)),
        # same non-numeric/non-string type on both sides is still an error
        ("true < false;", (1, 6)),
        ("nil > nil;", (1, 5)),
    ],
)
def test_comparison_operand_error(run, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("1 / 0;", (1, 3)),
        ("5 / 0;", (1, 3)),
        ("10 / 0;", (1, 4)),
        ("1 / (2 - 2);", (1, 3)),
    ],
)
def test_division_by_zero_error(run, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Division by zero"
    assert excinfo.value.get_line_info() == position


@pytest.mark.parametrize(
    "source, position",
    [
        # unary minus requires a number
        ("-true;", (1, 1)),
        ("-false;", (1, 1)),
        ("-nil;", (1, 1)),
        ('-"a";', (1, 1)),
    ],
)
def test_unary_negation_error(run, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operand must be a number"
    assert excinfo.value.get_line_info() == position
