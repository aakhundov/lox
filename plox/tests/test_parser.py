import pytest

from plox.ast import Expression
from plox.ast_printer import AstPrinter
from plox.parser import ParserError


@pytest.fixture
def parse(parse_program):
    """Return a helper that parses `source` into a tuple of Stmt.

    The parser hands back a `Program`; the tests are about the statements in
    it, so the helper unwraps it.
    """

    def _parse(source):
        return parse_program(source).statements

    return _parse


@pytest.fixture
def parse_errors(collect_errors, parse):
    """Return a helper that parses `source` expecting failure.

    The parser recovers from each error and carries on, so a single source can
    yield several.
    """

    def _parse_errors(source):
        return collect_errors(ParserError, parse, source)

    return _parse_errors


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
        ("a and b;", "(and a b)"),
        ("a or b;", "(or a b)"),
        # each is left-associative
        ("a and b and c;", "(and (and a b) c)"),
        ("a or b or c;", "(or (or a b) c)"),
        # `and` binds tighter than `or`
        ("a or b and c;", "(or a (and b c))"),
        ("a and b or c;", "(or (and a b) c)"),
        # equality binds tighter than `and`/`or`
        ("1 == 2 and 3;", "(and (== 1 2) 3)"),
        # `and`/`or` sit above assignment
        ("a = b or c;", "(= a (or b c))"),
    ],
)
def test_logical(show_expr, source, expected):
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
        ("x;", "x"),
        ("foo;", "foo"),
        # a variable is a primary, so it composes like any other operand
        ("foo + bar;", "(+ foo bar)"),
        ("-x;", "(- x)"),
        ("x * y + z;", "(+ (* x y) z)"),
    ],
)
def test_variable(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("a ? b : c;", "(?: a b c)"),
        # the else-branch is right-associative
        ("a ? b : c ? d : e;", "(?: a b (?: c d e))"),
        # the middle branch is a full expression, delimited by ':', so a
        # nested conditional may sit there too
        ("a ? b ? c : d : e;", "(?: a (?: b c d) e)"),
        # `or` binds tighter than `?:`, so it forms the operands
        ("a or b ? c : d;", "(?: (or a b) c d)"),
        ("a ? b : c or d;", "(?: a b (or c d))"),
        # `?:` sits above assignment: it is an assignment's value
        ("x = a ? b : c;", "(= x (?: a b c))"),
        # every branch is a full expression
        ("a ? b + c : d * e;", "(?: a (+ b c) (* d e))"),
    ],
)
def test_conditional(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, position",
    [
        # a '?' with no ':' to match it
        ("a ? b;", (1, 6)),
        # a stray token where the ':' was expected
        ("a ? b c;", (1, 7)),
    ],
)
def test_conditional_error(parse_errors, error_position, source, position):
    (error,) = parse_errors(source)
    assert str(error) == "Expect ':' to match ?"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        ("x = 1;", "(= x 1)"),
        ('name = "lox";', '(= name "lox")'),
        # assignment is right-associative
        ("x = y = 1;", "(= x (= y 1))"),
        # assignment sits below every other expression form
        ("x = 1 + 2;", "(= x (+ 1 2))"),
        ("a = b == c;", "(= a (== b c))"),
    ],
)
def test_assignment(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        ("1 + 2;", "(exp (+ 1 2))"),
        ('"hi";', '(exp "hi")'),
        ("nil;", "(exp nil)"),
        # the statement is transparent to the expression it wraps
        ("(1);", "(exp (grp 1))"),
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
        ("1; 2;", ["(exp 1)", "(exp 2)"]),
        (
            "print 1; 2 + 3; print 4;",
            ["(print 1)", "(exp (+ 2 3))", "(print 4)"],
        ),
        # empty source is legal and yields no statements
        ("", []),
    ],
)
def test_statement_sequence(show_all, source, expected):
    assert show_all(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a declaration with no initializer
        ("var x;", "(var x)"),
        ("var x = 1;", "(var x 1)"),
        ('var name = "lox";', '(var name "lox")'),
        # the initializer is a full expression
        ("var x = 1 + 2;", "(var x (+ 1 2))"),
    ],
)
def test_var_declaration(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # an empty block has no statements
        ("{}", "(blk)"),
        ("{ print 1; }", "(blk (print 1))"),
        # a block groups its statements in source order
        ("{ 1; 2; }", "(blk (exp 1) (exp 2))"),
        # blocks may contain declarations
        ("{ var x = 1; print x; }", "(blk (var x 1) (print x))"),
        # blocks nest
        ("{ { 1; } }", "(blk (blk (exp 1)))"),
    ],
)
def test_block(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # an `if` with no `else` omits the third child
        ("if (true) print 1;", "(if true (print 1))"),
        ("if (x) print 1; else print 2;", "(if x (print 1) (print 2))"),
        # the condition is a full expression
        ("if (1 < 2) print 1;", "(if (< 1 2) (print 1))"),
        # either branch may be a block
        (
            "if (a) { print 1; } else { print 2; }",
            "(if a (blk (print 1)) (blk (print 2)))",
        ),
        # a dangling `else` binds to the nearest `if`
        (
            "if (a) if (b) print 1; else print 2;",
            "(if a (if b (print 1) (print 2)))",
        ),
    ],
)
def test_if_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # the condition must be parenthesized
        ("if true) print 1;", "Expect '(' after if", (1, 4)),
        ("if", "Expect '(' after if", (1, 3)),
        # the condition's closing paren must be present
        ("if (true print 1;", "Expect ')' after if condition", (1, 10)),
    ],
)
def test_if_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        ("while (i < 3) print i;", "(while (< i 3) (print i))"),
        # the body may be a block
        ("while (true) { print 1; }", "(while true (blk (print 1)))"),
    ],
)
def test_while_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # the condition must be parenthesized
        ("while true) print 1;", "Expect '(' after while", (1, 7)),
        ("while", "Expect '(' after while", (1, 6)),
        # the condition's closing paren must be present
        ("while (true print 1;", "Expect ')' after while condition", (1, 13)),
    ],
)
def test_while_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # all three clauses present
        (
            "for (var i = 0; i < 3; i = i + 1) print i;",
            "(for (var i 0) (< i 3) (= i (+ i 1)) (print i))",
        ),
        # a non-var initializer is an expression statement
        (
            "for (i = 0; i < 3; i = i + 1) print i;",
            "(for (exp (= i 0)) (< i 3) (= i (+ i 1)) (print i))",
        ),
        # every clause is optional; an omitted one renders as nil
        ("for (;;) print 1;", "(for nil nil nil (print 1))"),
        ("for (; i < 3;) print i;", "(for nil (< i 3) nil (print i))"),
        (
            "for (var i = 0;; i = i + 1) print i;",
            "(for (var i 0) nil (= i (+ i 1)) (print i))",
        ),
        ("for (var i = 0; i < 3;) print i;", "(for (var i 0) (< i 3) nil (print i))"),
    ],
)
def test_for_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # the clauses must be parenthesized
        ("for", "Expect '(' after for", (1, 4)),
        # the condition clause is terminated by ';'
        ("for (;true", "Expect ';' after for condition", (1, 11)),
        (
            "for (var i = 0; i < 3 i = i + 1) print i;",
            "Expect ';' after for condition",
            (1, 23),
        ),
        # the clause list is closed by ')'
        (
            "for (var i = 0; i < 3; i = i + 1 print i;",
            "Expect ')' after for clauses",
            (1, 34),
        ),
    ],
)
def test_for_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # break/continue are statements in a loop body
        ("while (true) break;", "(while true (break))"),
        ("while (true) continue;", "(while true (continue))"),
        ("for (;;) break;", "(for nil nil nil (break))"),
        ("for (;;) continue;", "(for nil nil nil (continue))"),
        # they may sit inside a block body
        ("while (true) { break; }", "(while true (blk (break)))"),
        # ...or nested in another statement, as long as a loop encloses them
        ("while (true) if (x) continue;", "(while true (if x (continue)))"),
        # a jump binds to its lexically enclosing loop; loops nest
        (
            "while (a) { while (b) break; }",
            "(while a (blk (while b (break))))",
        ),
    ],
)
def test_loop_jump_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # the jump must be terminated by ';'
        ("while (true) break", "Expect ';' after break", (1, 19)),
        ("while (true) continue", "Expect ';' after continue", (1, 22)),
    ],
)
def test_loop_jump_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # a class with no methods is legal and has no children
        ("class A {}", "(class A)"),
        # methods are functions declared without the `fun` keyword
        ("class A { m() {} }", "(class A (fun m ()))"),
        (
            "class A { m(a, b) { return a + b; } }",
            "(class A (fun m (a, b) (return (+ a b))))",
        ),
        # several methods keep source order
        ("class A { a() {} b() {} }", "(class A (fun a ()) (fun b ()))"),
        # `init` is parsed as an ordinary method; nothing distinguishes it here
        (
            "class A { init(x) { this.x = x; } }",
            "(class A (fun init (x) (exp (set x this x))))",
        ),
        # a class is a declaration, so it nests wherever one may
        ("{ class A {} }", "(blk (class A))"),
        ("fun f() { class A {} }", "(fun f () (class A))"),
        # `<` names a superclass, which renders after the class's own name
        ("class B < A {}", "(class B A)"),
        ("class B < A { m() {} }", "(class B A (fun m ()))"),
        # the superclass is an identifier, not an arbitrary expression, so
        # whether it names a class at all is a runtime question
        ("class B < undeclared {}", "(class B undeclared)"),
        # a subclass nests like any other declaration
        ("{ class B < A {} }", "(blk (class B A))"),
    ],
)
def test_class_declaration(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        ("class {}", "Expect class name", (1, 7)),
        ("class A ()", "Expect '{' before class body", (1, 9)),
        # the body must be closed; at EOF the error points just past the source
        ("class A {", "Expect '}' after class body", (1, 10)),
        # a class body holds methods only, so anything else wants a method name
        ("class A { 1 }", "Expect method name", (1, 11)),
        # a method's signature is checked like a function's, but reports "method"
        ("class A { m {} }", "Expect '(' after method name", (1, 13)),
        ("class A { m(1) {} }", "Expect parameter name", (1, 13)),
        # `<` must be followed by the superclass's name...
        ("class B < {}", "Expect superclass name", (1, 11)),
        # ...which is an identifier, not any other token
        ("class B < 1 {}", "Expect superclass name", (1, 11)),
        ("class B < this {}", "Expect superclass name", (1, 11)),
        # only one superclass may be named: Lox has no multiple inheritance
        ("class C < A, B {}", "Expect '{' before class body", (1, 12)),
        # and the `<` is not optional -- without it the name is not a superclass
        ("class B A {}", "Expect '{' before class body", (1, 9)),
    ],
)
def test_class_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # recovery abandons the whole class declaration, so the '}' that would
        # have closed the body is left over and reported as a second error
        (
            "class A { var x; }",
            [("Expect method name", (1, 11)), ("Expect expression", (1, 18))],
        ),
        (
            "class A { print 1; }",
            [("Expect method name", (1, 11)), ("Expect expression", (1, 20))],
        ),
        (
            "class A { m() ; }",
            [
                ("Expect '{' before method body", (1, 15)),
                ("Expect expression", (1, 17)),
            ],
        ),
    ],
)
def test_class_error_recovery(parse_errors, error_position, source, expected):
    # the first error is the real one; the parser then synchronizes to the next
    # declaration keyword or ';', which is past the end of the class body
    errors = parse_errors(source)
    assert [(str(e), error_position(e)) for e in errors] == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the signature renders in the head, the body as children
        ("fun f() {}", "(fun f ())"),
        ("fun f(a) { print a; }", "(fun f (a) (print a))"),
        ("fun f(a, b) { return a + b; }", "(fun f (a, b) (return (+ a b)))"),
        # a multi-statement body keeps source order
        ("fun f() { print 1; print 2; }", "(fun f () (print 1) (print 2))"),
        # declarations nest: a function may be declared inside another
        (
            "fun outer() { fun inner() { return 1; } }",
            "(fun outer () (fun inner () (return 1)))",
        ),
        # a function body is a block, so it may declare and loop freely
        (
            "fun f() { while (true) { break; } }",
            "(fun f () (while true (blk (break))))",
        ),
    ],
)
def test_function_declaration(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        ("fun () {}", "Expect function name", (1, 5)),
        ("fun f {}", "Expect '(' after function name", (1, 7)),
        # a parameter list holds identifiers only...
        ("fun f(1) {}", "Expect parameter name", (1, 7)),
        # ...and has no trailing comma
        ("fun f(a,) {}", "Expect parameter name", (1, 9)),
        ("fun f(a {}", "Expect ')' after function parameters", (1, 9)),
        # the body must be a braced block, not an arbitrary statement
        ("fun f() ;", "Expect '{' before function body", (1, 9)),
    ],
)
def test_function_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        ("fun f() { return 1; }", "(fun f () (return 1))"),
        # the value is optional
        ("fun f() { return; }", "(fun f () (return))"),
        # it is a full expression
        ("fun f() { return a + b * c; }", "(fun f () (return (+ a (* b c))))"),
        # `return` may sit anywhere a statement may, as long as a function
        # body encloses it
        (
            "fun f() { if (a) return 1; return 2; }",
            "(fun f () (if a (return 1)) (return 2))",
        ),
        (
            "fun f() { while (true) { return 1; } }",
            "(fun f () (while true (blk (return 1))))",
        ),
    ],
)
def test_return_statement(show_one, source, expected):
    assert show_one(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # the value must be terminated by ';'
        ("fun f() { return 1 }", "Expect ';' after return value", (1, 20)),
    ],
)
def test_return_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        ("f();", "(call f)"),
        ("f(1);", "(call f 1)"),
        ("f(1, 2, 3);", "(call f 1 2 3)"),
        # arguments are full expressions, evaluated as sub-trees
        ("f(1 + 2, a and b);", "(call f (+ 1 2) (and a b))"),
        # a call result is itself callable, so calls chain left-to-right
        ("f(1)(2);", "(call (call f 1) 2)"),
        ("f()();", "(call (call f))"),
        # calls nest as arguments
        ("f(g(1));", "(call f (call g 1))"),
        # call binds tighter than unary...
        ("-f(1);", "(- (call f 1))"),
        ("!f();", "(! (call f))"),
        # ...and tighter than any binary operator
        ("f(1) + g(2);", "(+ (call f 1) (call g 2))"),
        ("a * f(1);", "(* a (call f 1))"),
        # a parenthesized callee is a grouping, then called
        ("(f)();", "(call (grp f))"),
    ],
)
def test_call_expression(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        ("f(1;", "Expect ')' after call arguments", (1, 4)),
        ("f(1, 2;", "Expect ')' after call arguments", (1, 7)),
        # an argument list has no trailing comma: the parser expects another
        # expression after it
        ("f(1,);", "Expect expression", (1, 5)),
    ],
)
def test_call_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        ("a.b;", "(get b a)"),
        # `.` is left-associative, so a chain reads outside-in
        ("a.b.c;", "(get c (get b a))"),
        # property access and calls interleave at the same precedence level
        ("a.b();", "(call (get b a))"),
        ("a.b(1, 2);", "(call (get b a) 1 2)"),
        ("f().x;", "(get x (call f))"),
        ("a.b(1).c;", "(get c (call (get b a) 1))"),
        # `this` is a primary, so it takes properties like any other object
        ("this.x;", "(get x this)"),
        ("this.m();", "(call (get m this))"),
        # `.` binds tighter than unary...
        ("-a.b;", "(- (get b a))"),
        ("!a.b;", "(! (get b a))"),
        # ...and tighter than any binary operator
        ("a.b + c.d;", "(+ (get b a) (get d c))"),
        # a parenthesized object is a grouping, then accessed
        ("(a).b;", "(get b (grp a))"),
    ],
)
def test_property_access(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a `get` in assignment-target position becomes a `set`
        ("a.b = 1;", "(set b a 1)"),
        # only the last `.` becomes the setter; the rest stay getters
        ("a.b.c = 1;", "(set c (get b a) 1)"),
        # the object may be any expression that produced the getter
        ("f().x = 1;", "(set x (call f) 1)"),
        ("this.x = 1;", "(set x this 1)"),
        # the value is a full expression
        ("a.b = c + d;", "(set b a (+ c d))"),
        # assignment stays right-associative across property targets
        ("a.b = c.d = 1;", "(set b a (set d c 1))"),
        # a plain variable target is still a plain assignment
        ("x = a.b;", "(= x (get b a))"),
    ],
)
def test_property_assignment(show_expr, source, expected):
    assert show_expr(source) == expected


def test_this_expression(show_expr):
    # `this` parses anywhere a primary may appear; whether it is *legal* there
    # is a resolver question, not a grammatical one
    assert show_expr("this;") == "this"
    assert show_expr("f(this);") == "(call f this)"


@pytest.mark.parametrize(
    "source, expected",
    [
        # `super.name` is one primary: the grammar admits no bare `super`, so
        # the method name is folded into the node rather than hung off it
        ("super.m;", "super.m"),
        ("super.m();", "(call super.m)"),
        ("super.m(1, 2);", "(call super.m 1 2)"),
        # like `this`, it parses wherever a primary may appear -- whether the
        # enclosing class actually has a superclass is a resolver question
        ("super.m() + 1;", "(+ (call super.m) 1)"),
        ("f(super.m);", "(call f super.m)"),
        # the result of a super call is an ordinary value, so access continues
        # off it as a normal property chain
        ("super.m().x;", "(get x (call super.m))"),
    ],
)
def test_super_expression(show_expr, source, expected):
    assert show_expr(source) == expected


@pytest.mark.parametrize(
    "source, message, position",
    [
        # `super` alone is not an expression: a method name must follow
        ("super;", "Expect '.' after super", (1, 6)),
        ("super();", "Expect '.' after super", (1, 6)),
        # a super method is a lookup, never an assignment target -- it names
        # something on the superclass, which holds no fields to write
        ("super.m = 1;", "Invalid assignment target", (1, 9)),
        # the name after the dot must be an identifier
        ("super.;", "Expect superclass method name", (1, 7)),
        ("super.1;", "Expect superclass method name", (1, 7)),
        ("super.this;", "Expect superclass method name", (1, 7)),
    ],
)
def test_super_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("a.;", (1, 3)),
        # the name must be an identifier, not any other token
        ("a.1;", (1, 3)),
        ('a."x";', (1, 3)),
        # a keyword is not a property name either
        ("a.this;", (1, 3)),
        # the check applies at every link of a chain
        ("a.b.;", (1, 5)),
    ],
)
def test_property_name_error(parse_errors, error_position, source, position):
    (error,) = parse_errors(source)
    assert str(error) == "Expect property name after '.'"
    assert error_position(error) == position


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
        # a keyword that does not begin an expression and is not a statement
        # form of its own
        ("else", (1, 1)),
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
        # a conditional missing its middle or else branch
        ("a ? : c;", (1, 5)),
        ("a ? b : ;", (1, 9)),
    ],
)
def test_expected_expression_error(parse_errors, error_position, source, position):
    (error,) = parse_errors(source)
    assert str(error) == "Expect expression"
    assert error_position(error) == position


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
def test_missing_closing_paren_error(parse_errors, error_position, source, position):
    (error,) = parse_errors(source)
    assert str(error) == "Expect ')' after expression"
    assert error_position(error) == position


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
def test_missing_semicolon_error(
    parse_errors, error_position, source, message, position
):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, message, position",
    [
        # `var` must be followed by a variable name
        ("var;", "Expect variable name", (1, 4)),
        ("var 1;", "Expect variable name", (1, 5)),
        # a declaration needs its terminating ';'
        ("var x", "Expect ';' after variable declaration", (1, 6)),
        ("var x 1;", "Expect ';' after variable declaration", (1, 7)),
    ],
)
def test_var_declaration_error(parse_errors, error_position, source, message, position):
    (error,) = parse_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # the target of `=` must be assignable (a variable); the error is
        # reported at the `=` token, not the left-hand expression
        ("1 = 2;", (1, 3)),
        ("(x) = 2;", (1, 5)),
        ("a + b = c;", (1, 7)),
        # a call is not assignable, even though it sits at the same precedence
        # level as the property access that is
        ("f() = 1;", (1, 5)),
        ("a.b() = 1;", (1, 7)),
        # `this` names a binding the program may not rebind
        ("this = 1;", (1, 6)),
    ],
)
def test_invalid_assignment_target_error(
    parse_errors, error_position, source, position
):
    (error,) = parse_errors(source)
    assert str(error) == "Invalid assignment target"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # a block must be closed with '}'
        ("{ 1;", (1, 5)),
        ("{ print 1;", (1, 11)),
        ("{ var x = 1;", (1, 13)),
    ],
)
def test_missing_closing_brace_error(parse_errors, error_position, source, position):
    (error,) = parse_errors(source)
    assert str(error) == "Expect '}' after block"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # each bad declaration is synchronized past, so all are reported
        (
            "var 1; var 2; print;",
            [
                ("Expect variable name", (1, 5)),
                ("Expect variable name", (1, 12)),
                ("Expect expression", (1, 20)),
            ],
        ),
        # an invalid assignment target does not desync the parser, so the
        # next statement's error is still collected
        (
            "1 = 2; 3 = 4;",
            [
                ("Invalid assignment target", (1, 3)),
                ("Invalid assignment target", (1, 10)),
            ],
        ),
        # a raised error recovers at the next ';' and keeps parsing
        (
            "1 +; 2 +;",
            [
                ("Expect expression", (1, 4)),
                ("Expect expression", (1, 9)),
            ],
        ),
        # with no ';' to recover on, synchronization instead resumes at the
        # next declaration/statement keyword -- here the second `var`
        (
            "var 1 var 2;",
            [
                ("Expect variable name", (1, 5)),
                ("Expect variable name", (1, 11)),
            ],
        ),
        # a control-flow keyword is a sync point too, so recovery lands on
        # `while` and the loop body's own error is still collected
        (
            "1 + while (true) print 2 +;",
            [
                ("Expect expression", (1, 5)),
                ("Expect expression", (1, 27)),
            ],
        ),
    ],
)
def test_multiple_errors(parse_errors, error_position, source, expected):
    errors = parse_errors(source)
    assert [(str(e), error_position(e)) for e in errors] == expected
