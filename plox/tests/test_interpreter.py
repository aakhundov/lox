import pytest

from plox.class_ import LoxClass
from plox.common import LoxCallable, LoxValue
from plox.instance import LoxInstance
from plox.interpreter import Interpreter, InterpreterError
from plox.library import get_library


@pytest.fixture
def run(resolve_program):
    """Return a helper that runs `source`, collecting the values it prints.

    The interpreter's `print_fn` seam is tapped with a raw collector, so each
    executed `print` appends its list of evaluated LoxValues (varargs are one
    group). Operating at the Python-implementation level lets tests observe the
    actual runtime values rather than their rendered text. A fresh interpreter
    runs each source, so no state leaks between cases.

    The interpreter only ever runs resolved ASTs: an unresolved `Variable` or
    `Assign` carries no distance, which the interpreter reads as "this is a
    global", so the source goes through the whole pipeline first.
    """

    def _run(source):
        collected: list[list[LoxValue]] = []
        Interpreter(print_fn=collected.append).interpret(resolve_program(source))
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
        # a variable evaluates to its initialized value
        ("var x = 1; print x;", 1.0),
        ('var s = "hi"; print s;', "hi"),
        # a declaration with no initializer defaults to nil
        ("var x; print x;", None),
        # a reference composes like any other operand
        ("var a = 2; var b = 3; print a * b;", 6.0),
    ],
)
def test_variable(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # assignment updates an existing variable
        ("var x = 1; x = 2; print x;", 2.0),
        # assignment is an expression that yields the assigned value
        ("var a = 1; print a = 2;", 2.0),
        # chained assignment updates every target
        ("var a = 1; var b = 2; a = b = 9; print a;", 9.0),
    ],
)
def test_assignment(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # an inner block shadows an outer variable, and the outer binding is
        # restored once the block exits
        (
            "var x = 1; { var x = 2; print x; } print x;",
            [[2.0], [1.0]],
        ),
        # assignment inside a block reaches out to the enclosing variable
        ("var x = 1; { x = 2; } print x;", [[2.0]]),
        # an inner block can read a variable from the enclosing scope
        ("var x = 1; { var y = 2; print x + y; }", [[3.0]]),
        # blocks nest arbitrarily deep
        ("{ { { print 1; } } }", [[1.0]]),
    ],
)
def test_scope(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a truthy condition runs the then-branch
        ("if (true) print 1;", [[1.0]]),
        ("if (true) print 1; else print 2;", [[1.0]]),
        # a falsey condition skips the then-branch (and runs any else)
        ("if (false) print 1;", []),
        ("if (false) print 1; else print 2;", [[2.0]]),
        # only the taken branch executes its side effects
        ("if (true) print 1; else print 2 / 0;", [[1.0]]),
        # plox truthiness: nil, false, 0, and "" are all falsey (see is_truthy)
        ("if (nil) print 1; else print 2;", [[2.0]]),
        ("if (0) print 1; else print 2;", [[2.0]]),
        ('if ("") print 1; else print 2;', [[2.0]]),
        # a nonzero number and nonempty string are truthy
        ("if (2) print 1;", [[1.0]]),
        # the condition is a full expression
        ("var x = 3; if (x > 2) print x;", [[3.0]]),
    ],
)
def test_if_statement(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # `and` returns the left operand when it is falsey, else the right
        ("print true and 2;", 2.0),
        ("print false and 2;", False),
        ("print nil and 2;", None),
        # `or` returns the left operand when it is truthy, else the right
        ("print 1 or 2;", 1.0),
        ("print false or 2;", 2.0),
        ("print nil or 2;", 2.0),
        # operands are values, not coerced to booleans
        ('print "a" and "b";', "b"),
        ("print false or nil;", None),
    ],
)
def test_logical(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # a falsey left operand short-circuits `and`: the assignment side
        # effect on the right never happens, so `a` keeps its value
        ("var a = 1; false and (a = 2); print a;", [[1.0]]),
        # a truthy left operand short-circuits `or`
        ("var a = 1; true or (a = 2); print a;", [[1.0]]),
        # the non-short-circuiting side IS evaluated
        ("var a = 1; true and (a = 2); print a;", [[2.0]]),
        ("var a = 1; false or (a = 2); print a;", [[2.0]]),
    ],
)
def test_logical_short_circuit(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the condition selects which branch's value is returned
        ("print true ? 1 : 2;", 1.0),
        ("print false ? 1 : 2;", 2.0),
        # plox truthiness: nil, false, 0, and "" all take the else-branch
        ("print nil ? 1 : 2;", 2.0),
        ("print 0 ? 1 : 2;", 2.0),
        ('print "" ? 1 : 2;', 2.0),
        # a nonzero number and nonempty string are truthy
        ("print 3 ? 1 : 2;", 1.0),
        ('print "x" ? 1 : 2;', 1.0),
        # the branch value keeps its runtime type
        ("print true ? nil : 1;", None),
        ('print 1 < 2 ? "y" : "n";', "y"),
        # the else-branch is right-associative, so this chains as a lookup
        ("print false ? 1 : true ? 2 : 3;", 2.0),
        ("print false ? 1 : false ? 2 : 3;", 3.0),
        # only the selected branch is evaluated, so the other's error is unseen
        ("print true ? 1 : 2 / 0;", 1.0),
        ("print false ? 1 / 0 : 2;", 2.0),
    ],
)
def test_conditional(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # the untaken branch's side effect never runs
        ("var a = 1; true ? nil : (a = 2); print a;", [[1.0]]),
        ("var a = 1; false ? (a = 2) : nil; print a;", [[1.0]]),
        # the taken branch IS evaluated
        ("var a = 1; true ? (a = 2) : nil; print a;", [[2.0]]),
        ("var a = 1; false ? nil : (a = 2); print a;", [[2.0]]),
    ],
)
def test_conditional_short_circuit(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the body runs once per iteration while the condition holds
        ("var i = 0; while (i < 3) { print i; i = i + 1; }", [[0.0], [1.0], [2.0]]),
        # a condition false at entry runs the body zero times
        ("while (false) print 1;", []),
    ],
)
def test_while_statement(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # counts up via the three header clauses
        ("for (var i = 0; i < 3; i = i + 1) print i;", [[0.0], [1.0], [2.0]]),
        # a condition false at entry runs the body zero times
        ("for (var i = 0; i > 3; i = i + 1) print i;", []),
        # omitted init/increment: the loop drives itself from the body
        ("var i = 0; for (; i < 2;) { print i; i = i + 1; }", [[0.0], [1.0]]),
        # a non-var initializer assigns to an existing outer variable
        ("var i = 9; for (i = 0; i < 2; i = i + 1) print i;", [[0.0], [1.0]]),
    ],
)
def test_for_statement(run, source, expected):
    assert run(source) == expected


def test_for_initializer_scope(run, error_position):
    # a `var` initializer is scoped to the loop and does not leak past it
    with pytest.raises(InterpreterError) as excinfo:
        run("for (var i = 0; i < 1; i = i + 1) print i; print i;")
    assert str(excinfo.value) == "Undefined variable: i"
    assert error_position(excinfo.value) == (1, 50)


@pytest.mark.parametrize(
    "source, expected",
    [
        # `break` exits the loop immediately, skipping the rest of the body
        # and every remaining iteration
        (
            "var i = 0; while (i < 5) { i = i + 1; if (i == 3) break; print i; }",
            [[1.0], [2.0]],
        ),
        # it behaves the same inside a `for` loop
        (
            "for (var i = 0; i < 5; i = i + 1) { if (i == 3) break; print i; }",
            [[0.0], [1.0], [2.0]],
        ),
        # a `break` nested inside a block still exits the loop (and does not
        # spin forever)
        ("while (true) { { break; } }", []),
        # `break` exits only the innermost loop; the outer one keeps iterating
        (
            "for (var i = 0; i < 2; i = i + 1) {"
            " for (var j = 0; j < 5; j = j + 1) { if (j == 1) break; print j; }"
            " print i; }",
            [[0.0], [0.0], [0.0], [1.0]],
        ),
    ],
)
def test_break(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # `continue` skips the rest of the body and re-tests the condition
        (
            "var i = 0; while (i < 5) { i = i + 1; if (i == 3) continue; print i; }",
            [[1.0], [2.0], [4.0], [5.0]],
        ),
        # in a `for` loop, `continue` still runs the increment clause, so the
        # loop keeps making progress
        (
            "for (var i = 0; i < 5; i = i + 1) { if (i == 2) continue; print i; }",
            [[0.0], [1.0], [3.0], [4.0]],
        ),
        # `continue` re-tests the condition rather than looping forever
        ("var i = 0; while (i < 3) { i = i + 1; continue; print 9; }", []),
        # a block on the skipped path is unwound cleanly: its scope is restored
        # so the next iteration's declaration starts fresh
        (
            "for (var i = 0; i < 3; i = i + 1)"
            " { var x = i; if (x == 1) continue; print x; }",
            [[0.0], [2.0]],
        ),
    ],
)
def test_continue(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a call runs the body; a parameterless function takes no arguments
        ("fun f() { print 1; } f();", [[1.0]]),
        # arguments bind to parameters positionally
        ("fun f(a) { print a; } f(1);", [[1.0]]),
        ("fun f(a, b, c) { print a, b, c; } f(1, 2, 3);", [[1.0, 2.0, 3.0]]),
        # arguments are evaluated before the call
        ("fun f(a) { print a; } f(1 + 2);", [[3.0]]),
        # each call re-binds the parameters, so calls do not interfere
        ("fun f(a) { print a; } f(1); f(2);", [[1.0], [2.0]]),
        # a declaration alone runs nothing
        ("fun f() { print 1; }", []),
    ],
)
def test_function_call(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # `return` hands the value back to the call site
        ("fun f() { return 1; } print f();", 1.0),
        ("fun f(a, b) { return a + b; } print f(1, 2);", 3.0),
        # a bare `return` and falling off the end both produce nil
        ("fun f() { return; } print f();", None),
        ("fun f() { } print f();", None),
        # the first `return` wins; later statements never run
        ("fun f() { return 1; return 2; } print f();", 1.0),
        # `return` unwinds out of nested blocks...
        ("fun f() { { { return 3; } } } print f();", 3.0),
        # ...and out of a loop, without the loop swallowing it
        ("fun f() { while (true) { return 4; } } print f();", 4.0),
        (
            "fun f() { for (var i = 0; i < 5; i = i + 1) { if (i == 2) return i; } }"
            " print f();",
            2.0,
        ),
    ],
)
def test_return_value(value, source, expected):
    _assert_value(value(source), expected)


def test_return_skips_remaining_statements(run):
    # statements after the taken `return` do not execute
    assert run("fun f() { print 1; return; print 2; } f();") == [[1.0]]


@pytest.mark.parametrize(
    "source, expected",
    [
        # a function captures the environment it was declared in, not the one
        # it is called from
        (
            "fun mk() { var c = 1; fun get() { return c; } return get; }"
            " var g = mk(); print g();",
            [[1.0]],
        ),
        # the captured environment is live: closing over a variable shares it
        (
            "fun mk() { var c = 0; fun inc() { c = c + 1; return c; } return inc; }"
            " var i = mk(); print i(); print i(); print i();",
            [[1.0], [2.0], [3.0]],
        ),
        # each call to the maker gets its own captured environment
        (
            "fun mk() { var c = 0; fun inc() { c = c + 1; return c; } return inc; }"
            " var a = mk(); var b = mk(); print a(); print a(); print b();",
            [[1.0], [2.0], [1.0]],
        ),
    ],
)
def test_closure(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a name binds to the declaration in scope where the code was written,
        # not to whatever the name happens to mean when the code runs: both
        # calls print the global, even though the second one runs after a local
        # `a` has been declared in the same block
        (
            """
            var a = "global";
            {
                fun showA() { print a; }
                showA();
                var a = "block";
                showA();
            }
            """,
            [["global"], ["global"]],
        ),
        # the same for a block that is entered twice: the later declaration
        # never captures the earlier call
        (
            """
            var a = "global";
            fun showA() { print a; }
            { showA(); var a = 1; showA(); }
            """,
            [["global"], ["global"]],
        ),
        # a local declared after a nested block does not become visible to it
        (
            'var a = "global"; { { print a; } var a = "block"; }',
            [["global"]],
        ),
    ],
)
def test_static_scope(run, source, expected):
    assert run(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # functions are first-class values: passable as arguments...
        (
            "fun ap(g, x) { return g(x); } fun d(v) { return v * 2; } print ap(d, 21);",
            42.0,
        ),
        # ...and callable directly off the returning call
        ("fun mk() { fun n() { return 7; } return n; } print mk()();", 7.0),
        # recursion resolves the function through the enclosing scope
        (
            "fun fact(n) { if (n <= 1) return 1; return n * fact(n - 1); } print fact(5);",
            120.0,
        ),
        (
            "fun fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }"
            " print fib(10);",
            55.0,
        ),
        # mutual recursion works because both names are defined before the call
        (
            "fun even(n) { if (n == 0) return true; return odd(n - 1); }"
            " fun odd(n) { if (n == 0) return false; return even(n - 1); }"
            " print even(4);",
            True,
        ),
    ],
)
def test_higher_order_and_recursion(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # a function body has its own scope: locals do not leak to the caller
        ("fun f() { var y = 1; } f(); print y;", "Undefined variable: y"),
        # nor do parameters
        ("fun f(a) { } f(1); print a;", "Undefined variable: a"),
    ],
)
def test_function_body_scope(run, source, expected):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a parameter shadows an outer variable of the same name...
        ("var x = 1; fun f(x) { return x; } print f(9);", 9.0),
        # ...without disturbing it
        ("var x = 1; fun f(x) { return x; } f(9); print x;", 1.0),
        # arguments pass by value: reassigning a parameter is local to the call
        ("fun f(a) { a = a + 1; return a; } var v = 1; f(v); print v;", 1.0),
    ],
)
def test_parameter_scope(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # a function value renders with its declared name
        ("fun f() {} print f;", "<fn f>"),
        ("fun some_name(a, b) {} print some_name;", "<fn some_name>"),
        # native functions are marked as such
        ("print clock;", "<fn clock (native)>"),
    ],
)
def test_function_value_display(value, source, expected):
    result = value(source)
    assert isinstance(result, LoxCallable)
    assert str(result) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a class value renders with its declared name
        ("class A {} print A;", "<class A>"),
        ("class SomeName {} print SomeName;", "<class SomeName>"),
    ],
)
def test_class_value_display(value, source, expected):
    result = value(source)
    # a class is callable: calling it is how an instance is made
    assert isinstance(result, LoxCallable)
    assert str(result) == expected


def test_instance_value_display(value):
    result = value("class A {} print A();")
    assert isinstance(result, LoxInstance)
    assert str(result) == "<instance of <class A>>"


@pytest.mark.parametrize(
    "source, expected",
    [
        # a subclass names its superclass alongside its own name
        ("class A {} class B < A {} print B;", "<class B (A)>"),
        # only the immediate superclass shows, however deep the chain
        ("class A {} class B < A {} class C < B {} print C;", "<class C (B)>"),
        # the base of the chain still renders as a plain class
        ("class A {} class B < A {} print A;", "<class A>"),
    ],
)
def test_subclass_value_display(value, source, expected):
    result = value(source)
    assert isinstance(result, LoxClass)
    assert str(result) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a field springs into being when it is first assigned
        ("class A {} var a = A(); a.x = 1; print a.x;", 1.0),
        ('class A {} var a = A(); a.x = "s"; print a.x;', "s"),
        ("class A {} var a = A(); a.x = true; print a.x;", True),
        ("class A {} var a = A(); a.x = nil; print a.x;", None),
        # assigning again replaces the value
        ("class A {} var a = A(); a.x = 1; a.x = 2; print a.x;", 2.0),
        # a set is an expression, evaluating to the assigned value
        ("class A {} var a = A(); print a.x = 1;", 1.0),
        # fields hold arbitrary expressions, evaluated before the store
        ("class A {} var a = A(); a.x = 1 + 2; print a.x;", 3.0),
        # ...including other instances, so structures nest
        ("class A {} var a = A(); a.x = A(); a.x.y = 1; print a.x.y;", 1.0),
    ],
)
def test_instance_fields(value, source, expected):
    _assert_value(value(source), expected)


def test_fields_are_per_instance(run):
    # each instance carries its own field table, so writing one leaves the
    # other alone -- they share only the class
    assert run("""
        class A {}
        var a = A();
        var b = A();
        a.x = 1;
        b.x = 2;
        print a.x, b.x;
    """) == [[1.0, 2.0]]


@pytest.mark.parametrize(
    "source, expected",
    [
        # a method is reached through the instance and called like a function
        ("class A { m() { return 1; } } print A().m();", 1.0),
        ("class A { m(a, b) { return a + b; } } print A().m(1, 2);", 3.0),
        # a method with no explicit return yields nil, as any function does
        ("class A { m() {} } print A().m();", None),
        # `this` inside a method is the instance it was reached through
        (
            "class A { get_() { return this.x; } } var a = A(); a.x = 1; print a.get_();",
            1.0,
        ),
        # a method may write fields through `this`
        (
            "class A { set_(v) { this.x = v; } get_() { return this.x; } }"
            " var a = A(); a.set_(2); print a.get_();",
            2.0,
        ),
        # one method may call another on the same instance
        (
            "class A { one() { return 1; } two() { return this.one() + 1; } }"
            " print A().two();",
            2.0,
        ),
    ],
)
def test_method_call(value, source, expected):
    _assert_value(value(source), expected)


def test_method_returning_an_instance_chains(value):
    # a method may name its own class and build more of them, and the result
    # is an ordinary instance, so calls chain off it
    result = value("class A { copy() { return A(); } } print A().copy().copy();")
    assert isinstance(result, LoxInstance)


def test_method_sees_its_own_instance(run):
    # the same method reached through two instances resolves `this` to
    # whichever one it was reached through
    assert run("""
        class A {
            init(v) { this.v = v; }
            get_() { return this.v; }
        }
        var a = A(1);
        var b = A(2);
        print a.get_(), b.get_();
    """) == [[1.0, 2.0]]


def test_bound_method_keeps_its_instance(run):
    # reading a method off an instance binds it there and then, so it stays
    # attached to that instance however it is later passed around and called
    assert run("""
        class A {
            init(v) { this.v = v; }
            get_() { return this.v; }
        }
        var a = A(1);
        var m = a.get_();
        var f = A(2).get_;
        print m, f();
    """) == [[1.0, 2.0]]


def test_field_shadows_method(value):
    # a property lookup checks fields before methods, so a field of the same
    # name hides the method rather than colliding with it
    _assert_value(
        value("class A { m() { return 1; } } var a = A(); a.m = 2; print a.m;"), 2.0
    )


@pytest.mark.parametrize(
    "source, expected",
    [
        # `init` runs when the class is called, with the arguments given
        ("class A { init() { this.x = 1; } } print A().x;", 1.0),
        ("class A { init(v) { this.x = v; } } print A(2).x;", 2.0),
        ("class A { init(a, b) { this.x = a + b; } } print A(1, 2).x;", 3.0),
        # calling `init` again re-runs it on the existing instance
        (
            "class A { init(v) { this.x = v; } } var a = A(1); a.init(2); print a.x;",
            2.0,
        ),
    ],
)
def test_initializer(value, source, expected):
    _assert_value(value(source), expected)


def test_initializer_bare_return_exits_early(run):
    # a bare `return` leaves the initializer without running the rest of it,
    # and without preventing the instance from being handed back
    collected = run("""
        class A { init() { return; this.x = 1; } }
        print A();
    """)
    ((instance,),) = collected
    assert isinstance(instance, LoxInstance)
    # the statement after the `return` never ran, so there is no field
    with pytest.raises(InterpreterError) as excinfo:
        run("class A { init() { return; this.x = 1; } } print A().x;")
    assert str(excinfo.value) == "Undefined property 'x'"


def test_initializer_returns_the_instance(run):
    # a class call evaluates to the new instance whatever `init` does, and an
    # explicit `init` call returns that same instance rather than nil
    collected = run("""
        class A { init() { this.x = 1; } }
        var a = A();
        print a, a.init(), a.init().x;
    """)
    (group,) = collected
    instance, returned, field = group
    assert isinstance(instance, LoxInstance)
    # the same object, not merely an equal one
    assert returned is instance
    _assert_value(field, 1.0)


def test_class_declaration_allows_self_reference(value):
    # the name is bound before the methods are built, so a method body may
    # mention the class it belongs to
    result = value("class A { me() { return A; } } print A().me();")
    assert isinstance(result, LoxClass)
    assert str(result) == "<class A>"


def test_method_closes_over_enclosing_scope(run):
    # a method is a function like any other: its closure is the scope the class
    # was declared in, with the `this` binding layered on top
    assert run("""
        fun make(v) {
            class A {
                get_() { return v; }
                both() { fun inner() { return v; } return inner(); }
            }
            return A();
        }
        print make(1).get_(), make(2).both();
    """) == [[1.0, 2.0]]


@pytest.mark.parametrize(
    "source, expected",
    [
        # a method not found on the class is looked for on its superclass
        ("class A { m() { return 1; } } class B < A {} print B().m();", 1.0),
        # the search walks the whole chain, not just one link
        (
            "class A { m() { return 1; } } class B < A {} class C < B {}"
            " print C().m();",
            1.0,
        ),
        # a subclass method of the same name overrides rather than collides
        (
            "class A { m() { return 1; } } class B < A { m() { return 2; } }"
            " print B().m();",
            2.0,
        ),
        # overriding is per-name: the other inherited methods still resolve
        (
            "class A { one() { return 1; } two() { return 2; } }"
            " class B < A { two() { return 3; } } print B().one() + B().two();",
            4.0,
        ),
        # an inherited method runs with `this` bound to the subclass instance,
        # so a call it makes dispatches back to the override
        (
            "class A { m() { return this.n(); } n() { return 1; } }"
            " class B < A { n() { return 2; } } print B().m();",
            2.0,
        ),
        # the superclass keeps its own behaviour: overriding does not mutate it
        (
            "class A { m() { return 1; } } class B < A { m() { return 2; } }"
            " print A().m();",
            1.0,
        ),
    ],
)
def test_method_inheritance(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # an initializer is inherited like any other method, arity included
        ("class A { init(v) { this.x = v; } } class B < A {} print B(1).x;", 1.0),
        # a subclass initializer replaces it, and may chain through `super`
        (
            "class A { init(v) { this.x = v; } }"
            " class B < A { init(v) { super.init(v * 2); } } print B(1).x;",
            2.0,
        ),
        # the chained initializer writes to the same instance, so both classes'
        # fields end up on it
        (
            "class A { init() { this.a = 1; } }"
            " class B < A { init() { super.init(); this.b = 2; } }"
            " print B().a + B().b;",
            3.0,
        ),
    ],
)
def test_initializer_inheritance(value, source, expected):
    _assert_value(value(source), expected)


@pytest.mark.parametrize(
    "source, expected",
    [
        # `super.m()` reaches the superclass's method past the override
        (
            "class A { m() { return 1; } } class B < A { m() { return super.m() + 1; } }"
            " print B().m();",
            2.0,
        ),
        # it works from a method of any name, not just the overriding one
        (
            "class A { m() { return 1; } } class B < A { n() { return super.m(); } }"
            " print B().n();",
            1.0,
        ),
        # arguments pass through as they do to any call
        (
            "class A { m(a, b) { return a + b; } }"
            " class B < A { m(a, b) { return super.m(a, b) * 2; } } print B().m(1, 2);",
            6.0,
        ),
        # the method is bound to the current instance, so `this` inside the
        # superclass's body is still the subclass instance
        (
            "class A { m() { return this.x; } }"
            " class B < A { init() { this.x = 1; } n() { return super.m(); } }"
            " print B().n();",
            1.0,
        ),
        # reading it without calling gives a bound method, callable later
        (
            "class A { m() { return 1; } }"
            " class B < A { n() { var f = super.m; return f(); } } print B().n();",
            1.0,
        ),
    ],
)
def test_super_call(value, source, expected):
    _assert_value(value(source), expected)


def test_super_is_resolved_statically(run):
    # `super` means "the superclass of the class this method was *written* in",
    # not "the superclass of the object's class" -- otherwise Mid.test on a
    # Leaf would loop back to Leaf.m instead of reaching Base.m
    assert run("""
        class Base { m() { return "Base"; } }
        class Mid < Base {
            m() { return "Mid"; }
            test() { return super.m(); }
        }
        class Leaf < Mid { m() { return "Leaf"; } }
        print Leaf().test(), Mid().test();
    """) == [["Base", "Base"]]


def test_super_in_nested_scopes(run):
    # `super` is found by walking out the scope chain, so anything that opens a
    # scope between the use and the method body has to be counted -- a block, a
    # loop body, and a nested function all sit in the way
    assert run("""
        class A { m() { return 1; } }
        class B < A {
            blocky() { { { return super.m(); } } }
            loopy() { for (var i = 0; i < 1; i = i + 1) { return super.m(); } }
            nested() { fun f() { fun g() { return super.m(); } return g(); } return f(); }
        }
        var b = B();
        print b.blocky(), b.loopy(), b.nested();
    """) == [[1.0, 1.0, 1.0]]


def test_nested_class_binds_its_own_super(run):
    # a subclass declared inside another subclass's method gets its own `super`
    # scope, and the inner one wins for its own methods
    assert run("""
        class Outer { m() { return "Outer"; } }
        class Inner { m() { return "Inner"; } }
        class B < Outer {
            make() {
                class C < Inner { m() { return super.m(); } }
                return C().m();
            }
            own() { return super.m(); }
        }
        print B().make(), B().own();
    """) == [["Inner", "Outer"]]


def test_inherited_fields_are_per_instance(run):
    # inheritance shares methods, never state: two instances of a subclass
    # still carry their own field tables
    assert run("""
        class A { init(v) { this.v = v; } get_() { return this.v; } }
        class B < A {}
        print B(1).get_(), B(2).get_();
    """) == [[1.0, 2.0]]


def test_superclass_is_captured_at_declaration(run):
    # the superclass is the value the name held when the subclass was declared,
    # so rebinding the name afterwards leaves the subclass untouched
    assert run("""
        class A { m() { return 1; } }
        var Super_ = A;
        class B < Super_ {}
        Super_ = nil;
        print B().m();
    """) == [[1.0]]


# A representative call for each native, by name. The test below asserts
# this covers the library exactly, so adding a native fails here until it is
# given a meaningful call rather than being silently skipped.
_NATIVE_CALLS = {
    "clock": "clock()",
    "sleep": "sleep(0)",
}


def test_native_functions_are_callable(run, value):
    # every library native is registered in the global scope under its own
    # name and invocable from source. Calling one reaches LoxCallable.call by
    # a different path than a LoxFunction does, so it is worth exercising
    # here; what each native actually returns is checked in test_library.
    natives = {fn.name: fn for fn in get_library()}
    assert natives.keys() == _NATIVE_CALLS.keys()

    for name, call in _NATIVE_CALLS.items():
        result = value(f"print {name};")
        assert isinstance(result, LoxCallable)
        assert str(result) == str(natives[name])
        # the call completing is the assertion; the returned value belongs to
        # the native and is checked in test_library
        assert run(f"{call};") == []


@pytest.mark.parametrize(
    "source, message, position",
    [
        # a call must supply exactly as many arguments as the function
        # declares; the error sits at the call's opening paren
        ("fun f() {} f(1);", "Expected 0 arguments but got 1", (1, 13)),
        ("fun f(a) {} f();", "Expected 1 argument but got 0", (1, 14)),
        ("fun f(a, b) {} f(1);", "Expected 2 arguments but got 1", (1, 17)),
        ("fun f(a) {} f(1, 2);", "Expected 1 argument but got 2", (1, 14)),
        # natives are checked the same way
        ("clock(1);", "Expected 0 arguments but got 1", (1, 6)),
    ],
)
def test_call_arity_error(run, error_position, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == message
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, message, position",
    [
        # a native rejecting its arguments surfaces as an ordinary runtime
        # error rather than escaping as a Python one: the interpreter names
        # the callee and positions it at the call, and the native supplies
        # only the reason
        (
            'sleep("a");',
            "Error calling 'sleep': Argument must be a number",
            (1, 6),
        ),
        (
            "sleep(nil);",
            "Error calling 'sleep': Argument must be a number",
            (1, 6),
        ),
        (
            "sleep(-1);",
            "Error calling 'sleep': Argument must be a non-negative number",
            (1, 6),
        ),
    ],
)
def test_native_argument_error(run, error_position, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == message
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # only callables may be called; the error points at the opening paren
        ("var x = 1; x();", (1, 13)),
        ('"s"();', (1, 4)),
        ("true();", (1, 5)),
        ("nil();", (1, 4)),
        ("(1 + 2)();", (1, 8)),
    ],
)
def test_not_callable_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Can only call functions and methods"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # a runtime error carries the calls it unwound through: the outermost
        # call site first, the position that actually failed last
        (
            'fun a() {\n  return 1 + "x";\n}\nfun b() {\n  return a();\n}\nb();',
            [(7, 2), (5, 11), (2, 12)],
        ),
        # an error raised by the environment rather than by the interpreter
        # collects frames the same way
        (
            "fun a() {\n  print undefined;\n}\na();",
            [(4, 2), (2, 9)],
        ),
        # a native's error is positioned at the call that raised it, and the
        # enclosing calls still contribute their frames
        (
            'fun a() {\n  sleep("x");\n}\na();',
            [(4, 2), (2, 8)],
        ),
        # every activation of a recursive function contributes its own frame
        (
            'fun a(n) {\n  if (n > 0) return a(n - 1);\n  return 1 + "x";\n}\na(2);',
            [(5, 2), (2, 22), (2, 22), (3, 12)],
        ),
        # arguments are evaluated before the callee is entered, so a failure
        # there reports no frame for the call being built
        (
            'fun a(x) { return x; }\na(1 + "s");',
            [(2, 5)],
        ),
    ],
)
def test_error_stack(run, error_stack, source, expected):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert error_stack(excinfo.value) == expected


@pytest.mark.parametrize(
    "source",
    [
        # a function calling itself with no base case
        "fun f() { f(); } f();",
        # a base case the recursion moves away from instead of towards
        "fun f(n) { if (n > 0) return 0; return f(n - 1); } f(0);",
        # two functions calling each other
        "fun a() { b(); } fun b() { a(); } a();",
        # the function reached through a variable holding it
        "fun f() { var g = f; g(); } f();",
        # the recursive call sitting in argument position
        "fun f(x) { return f(x); } f(1);",
        # a local function recursing on its own name
        "fun outer() { fun inner() { inner(); } inner(); } outer();",
        # nested blocks in the body: they consume interpreter stack of their
        # own, so how many Lox calls fit is a property of the body's shape
        "fun f() { { { { f(); } } } } f();",
    ],
)
def test_recursion_depth_error(run, source):
    # Lox calls recurse on the stack the interpreter itself runs on, so
    # unbounded recursion exhausts it. That is reported as an ordinary runtime
    # error: expecting an InterpreterError *is* the assertion here, since a
    # Python RecursionError escaping the interpreter would fail the test.
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Maximum recursion depth exceeded"


def test_recursion_depth_error_stack(run, error_stack):
    # the error unwinds through every activation, so it carries the top-level
    # call first and then one frame per recursive call. How many frames that is
    # depends on the Python stack still available, so only their positions are
    # asserted: the recursion is what the trace is made of.
    with pytest.raises(InterpreterError) as excinfo:
        run("fun f() { f(); } f();")
    stack = error_stack(excinfo.value)
    assert stack[0] == (1, 19)
    assert set(stack[1:]) == {(1, 12)}
    assert len(stack) > 2


@pytest.mark.parametrize(
    "source, expected",
    [
        # recursion that terminates is unaffected, however deep it nests: only
        # exhausting the stack is an error, not recursing far
        ("fun f(n) { if (n <= 0) return 0; return f(n - 1) + 1; } print f(30);", 30.0),
        # a recursive sum, reached through a base case at the bottom
        ("fun s(n) { if (n <= 0) return 0; return n + s(n - 1); } print s(30);", 465.0),
        # mutual recursion that terminates
        (
            """
            fun even(n) { if (n == 0) return true; return odd(n - 1); }
            fun odd(n) { if (n == 0) return false; return even(n - 1); }
            print even(30);
            """,
            True,
        ),
    ],
)
def test_deep_recursion_within_limit(value, source, expected):
    _assert_value(value(source), expected)


def test_interpreter_usable_after_recursion_depth_error(resolve_program):
    # exhausting the stack costs the failed call, not the interpreter: the same
    # instance keeps its globals and goes on running afterwards. A fresh
    # interpreter per source would not show this, so one is driven by hand.
    collected: list[list[LoxValue]] = []
    interpreter = Interpreter(print_fn=collected.append)

    interpreter.interpret(resolve_program("var x = 1; fun f() { f(); }"))
    with pytest.raises(InterpreterError):
        interpreter.interpret(resolve_program("f();"))

    interpreter.interpret(resolve_program("print x; print 1 + 2;"))
    assert collected == [[1.0], [3.0]]
    # and the recursion still reports rather than crashes the second time
    with pytest.raises(InterpreterError) as excinfo:
        interpreter.interpret(resolve_program("f();"))
    assert str(excinfo.value) == "Maximum recursion depth exceeded"


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
def test_default_print_output(capsys, resolve_program, source, expected):
    Interpreter().interpret(resolve_program(source))
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
def test_plus_operand_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert error_position(excinfo.value) == position


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
def test_arithmetic_operand_error(run, error_position, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == message
    assert error_position(excinfo.value) == position


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
def test_comparison_operand_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operands must both be number or string"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("1 / 0;", (1, 3)),
        ("5 / 0;", (1, 3)),
        ("10 / 0;", (1, 4)),
        ("1 / (2 - 2);", (1, 3)),
    ],
)
def test_division_by_zero_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Division by zero"
    assert error_position(excinfo.value) == position


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
def test_unary_negation_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Operand must be a number"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # reading a variable that was never declared
        ("print x;", (1, 7)),
        # assigning to a variable that was never declared
        ("x = 1;", (1, 1)),
        # a name declared only inside a block is gone once the block exits
        ("{ var x = 1; } print x;", (1, 22)),
    ],
)
def test_undefined_variable_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Undefined variable: x"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # reading a property the instance has neither as a field nor a method;
        # the error sits at the property name
        ("class A {} var a = A(); a.x;", (1, 27)),
        # a field written on one instance is not on another
        ("class A {} var a = A(); var b = A(); a.x = 1; b.x;", (1, 49)),
        # an initializer that never assigned it leaves it undefined
        ("class A { init() {} } var a = A(); a.x;", (1, 38)),
    ],
)
def test_undefined_property_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Undefined property 'x'"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # only instances carry properties, so every other value rejects a get
        ("var a = 1; a.x;", (1, 14)),
        ('var a = "s"; a.x;', (1, 16)),
        ("var a = true; a.x;", (1, 17)),
        ("var a = nil; a.x;", (1, 16)),
        # a class is not an instance of itself: there are no static members
        ("class A {} A.x;", (1, 14)),
        # nor is a function, or a native
        ("fun f() {} f.x;", (1, 14)),
        ("clock.x;", (1, 7)),
    ],
)
def test_get_on_non_instance_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Only instances have properties"
    assert error_position(excinfo.value) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # the same holds for a set, with its own message
        ("var a = 1; a.x = 1;", (1, 14)),
        ('var a = "s"; a.x = 1;', (1, 16)),
        ("var a = nil; a.x = 1;", (1, 16)),
        ("class A {} A.x = 1;", (1, 14)),
        ("fun f() {} f.x = 1;", (1, 14)),
    ],
)
def test_set_on_non_instance_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Only instances have fields"
    assert error_position(excinfo.value) == position


def test_set_target_is_checked_before_the_value_runs(run):
    # the object is evaluated and rejected before the value expression is, so
    # a bad target reports its own error rather than the value's
    with pytest.raises(InterpreterError) as excinfo:
        run("var a = 1; a.x = undefined_name;")
    assert str(excinfo.value) == "Only instances have fields"


@pytest.mark.parametrize(
    "source, message, position",
    [
        # a class with no initializer takes no arguments
        ("class A {} A(1);", "Expected 0 arguments but got 1", (1, 13)),
        # otherwise the initializer's parameters set the arity
        ("class A { init(a) {} } A();", "Expected 1 argument but got 0", (1, 25)),
        ("class A { init(a, b) {} } A(1);", "Expected 2 arguments but got 1", (1, 28)),
        ("class A { init(a) {} } A(1, 2);", "Expected 1 argument but got 2", (1, 25)),
        # a method's own arity is checked like any function's
        ("class A { m() {} } A().m(1);", "Expected 0 arguments but got 1", (1, 25)),
    ],
)
def test_class_arity_error(run, error_position, source, message, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == message
    assert error_position(excinfo.value) == position


def test_calling_a_non_method_field_error(run, error_position):
    # a field holding a non-callable is not made callable by being reached
    # through an instance
    with pytest.raises(InterpreterError) as excinfo:
        run("class A {} var a = A(); a.x = 1; a.x();")
    assert str(excinfo.value) == "Can only call functions and methods"
    assert error_position(excinfo.value) == (1, 37)


@pytest.mark.parametrize(
    "source, expected",
    [
        # an error inside a method carries the frame for the call that reached
        # it, outermost first
        ("class A { m() { x; } } A().m();", [(1, 29), (1, 17)]),
        # an error inside an initializer carries the frame for the class call
        ("class A { init() { x; } } A();", [(1, 28), (1, 20)]),
        # one method calling another stacks a frame per call
        (
            "class A { one() { x; } two() { this.one(); } } A().two();",
            [(1, 55), (1, 40), (1, 19)],
        ),
    ],
)
def test_method_error_stack(run, error_stack, source, expected):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert error_stack(excinfo.value) == expected


@pytest.mark.parametrize(
    "source, position",
    [
        # the superclass is an ordinary expression at runtime, so anything may
        # be named there -- only a class may actually be inherited from
        ("var x = 1; class B < x {}", (1, 22)),
        ('var s = "s"; class B < s {}', (1, 24)),
        ("var n = nil; class B < n {}", (1, 24)),
        # a function is callable but is not a class
        ("fun f() {} class B < f {}", (1, 22)),
        ("class B < clock {}", (1, 11)),
        # nor is an instance of one: a class inherits from a class, not an
        # object built by it
        ("class A {} var a = A(); class B < a {}", (1, 35)),
    ],
)
def test_superclass_not_a_class_error(run, error_position, source, position):
    with pytest.raises(InterpreterError) as excinfo:
        run(source)
    assert str(excinfo.value) == "Superclass must be a class"
    assert error_position(excinfo.value) == position


def test_undefined_super_method_error(run, error_stack):
    # a `super` lookup searches the superclass chain and nothing else, so a
    # name the chain does not hold is an error at the method name itself --
    # even when the *subclass* defines it
    with pytest.raises(InterpreterError) as excinfo:
        run("class A {} class B < A { m() { super.nope(); } } B().m();")
    assert str(excinfo.value) == "Undefined property 'nope'"
    assert error_stack(excinfo.value) == [(1, 55), (1, 38)]

    with pytest.raises(InterpreterError) as excinfo:
        run("class A {} class B < A { m() { super.m(); } } B().m();")
    assert str(excinfo.value) == "Undefined property 'm'"


def test_super_error_stack(run, error_stack):
    # a super call is a call: it puts its own frame on the stack, so an error
    # in the superclass's body reports the whole chain, outermost first
    with pytest.raises(InterpreterError) as excinfo:
        run("class A { m() { x; } } class B < A { m() { super.m(); } } B().m();")
    assert error_stack(excinfo.value) == [(1, 64), (1, 51), (1, 17)]


def test_superclass_is_evaluated_before_the_body_runs(run):
    # the superclass is resolved when the declaration executes, not when the
    # class is first used, so a bad one fails even if nothing instantiates it
    with pytest.raises(InterpreterError) as excinfo:
        run("var x = 1; class B < x { m() { return 1; } }")
    assert str(excinfo.value) == "Superclass must be a class"
