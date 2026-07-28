"""Tests for the CodePrinter -- the visitor that renders an AST back to Lox.

The contract is narrow on purpose: CodePrinter renders a *parser-produced* AST
as source. It does not parenthesize by precedence and does not insert braces,
so the parentheses and blocks in its output are the ones the AST already
carries as Grouping and Block nodes. Every source here is therefore driven
through the real Scanner and Parser, and the suite's centre is the round-trip
property in `test_round_trip`: re-parsing the printed text must give back an
equal AST.
"""

import textwrap

import pytest

from plox.ast import Expression, Literal
from plox.code_printer import CodePrinter


def lox(text):
    """Return a multi-line snippet as the printer would emit it.

    Lets an expectation be written as indented Lox in the test body: the
    common indentation goes away and the surrounding blank lines with it,
    since the printer emits no trailing newline.
    """
    return textwrap.dedent(text).strip("\n")


@pytest.fixture
def show(parse_program):
    """Return a helper that renders `source` back to Lox source."""

    def _show(source):
        return CodePrinter().print(parse_program(source))

    return _show


@pytest.fixture
def show_expr(parse_program):
    """Return a helper that renders just the expression in `source`.

    The expression tests care about how a single node renders, not about the
    enclosing statement, so `source` is one expression statement and the
    helper prints its inner expression -- which also drives `print`'s Expr
    entry point, where the result is returned rather than emitted as a line.
    """

    def _show(source):
        (statement,) = parse_program(source).statements
        assert isinstance(statement, Expression)
        return CodePrinter().print(statement.expression)

    return _show


# --- statements ---


def test_class(show):
    # an empty class still opens and closes its body
    assert show("class A {}") == lox("""
        class A {
        }
    """)
    assert show("class A { m() {} }") == lox("""
        class A {
          m() {
          }
        }
    """)
    # methods render without the `fun` keyword, which is how they are declared
    assert show("class A { init(x) { this.x = x; } m() { return 1; } }") == lox("""
        class A {
          init(x) {
            this.x = x;
          }
          m() {
            return 1;
          }
        }
    """)


def test_class_with_superclass(show):
    assert show("class B < A {}") == lox("""
        class B < A {
        }
    """)
    assert show("class B < A { m() { return super.m(); } }") == lox("""
        class B < A {
          m() {
            return super.m();
          }
        }
    """)


def test_function(show):
    assert show("fun f() {}") == lox("""
        fun f() {
        }
    """)
    # parameters are comma-separated, and the brace hangs on the header line
    assert show("fun f(a, b) { return a + b; }") == lox("""
        fun f(a, b) {
          return a + b;
        }
    """)


def test_var(show):
    # a declaration with no initializer omits the `=`
    assert show("var x;") == "var x;"
    assert show("var x = 1;") == "var x = 1;"
    assert show("var x = 1 + 2;") == "var x = 1 + 2;"


def test_for(show):
    assert show("for (var i = 0; i < 3; i = i + 1) print i;") == lox("""
        for (var i = 0; i < 3; i = i + 1)
          print i;
    """)
    # a block body hangs off the header instead of dropping to its own line
    assert show("for (var i = 0; i < 3; i = i + 1) { print i; }") == lox("""
        for (var i = 0; i < 3; i = i + 1) {
          print i;
        }
    """)


@pytest.mark.parametrize(
    "initializer, condition, increment, expected",
    [
        ("var i = 0", "i < 3", "i = i + 1", "for (var i = 0; i < 3; i = i + 1)"),
        ("i = 0", "i < 3", "i = i + 1", "for (i = 0; i < 3; i = i + 1)"),
        ("", "i < 3", "i = i + 1", "for ( ; i < 3; i = i + 1)"),
        ("var i = 0", "", "i = i + 1", "for (var i = 0; ; i = i + 1)"),
        ("var i = 0", "i < 3", "", "for (var i = 0; i < 3; )"),
        ("", "", "", "for ( ; ; )"),
    ],
)
def test_for_clauses_may_be_omitted(show, initializer, condition, increment, expected):
    # each of the three clauses is optional; an omitted one leaves its
    # semicolon in place so the header keeps its shape
    source = f"for ({initializer}; {condition}; {increment}) print 1;"
    assert show(source).split("\n")[0] == expected


def test_if(show):
    # a braceless branch drops to the next line, indented
    assert show("if (a) print 1;") == lox("""
        if (a)
          print 1;
    """)
    assert show("if (a) { print 1; }") == lox("""
        if (a) {
          print 1;
        }
    """)


def test_if_else(show):
    # `else` starts its own line, so a braced branch reads as `}` then `else {`
    assert show("if (a) print 1; else print 2;") == lox("""
        if (a)
          print 1;
        else
          print 2;
    """)
    assert show("if (a) { print 1; } else { print 2; }") == lox("""
        if (a) {
          print 1;
        }
        else {
          print 2;
        }
    """)


def test_if_else_chain(show):
    # `else if` is an if statement as the else branch, so it nests one level
    assert show("if (a) print 1; else if (b) print 2; else print 3;") == lox("""
        if (a)
          print 1;
        else
          if (b)
            print 2;
          else
            print 3;
    """)


def test_print(show):
    assert show("print 1;") == "print 1;"
    # varargs stay one statement, comma-separated
    assert show("print a, b, c;") == "print a, b, c;"
    assert show('print "hi", 1, true, nil;') == 'print "hi", 1, true, nil;'


def test_return(show):
    assert show("fun f() { return; }") == lox("""
        fun f() {
          return;
        }
    """)
    assert show("fun f() { return a + 1; }") == lox("""
        fun f() {
          return a + 1;
        }
    """)


def test_while(show):
    assert show("while (a) b;") == lox("""
        while (a)
          b;
    """)
    assert show("while (a) { print 1; }") == lox("""
        while (a) {
          print 1;
        }
    """)


def test_loop_jump(show):
    assert show("while (a) { break; }") == lox("""
        while (a) {
          break;
        }
    """)
    assert show("while (a) { continue; }") == lox("""
        while (a) {
          continue;
        }
    """)


def test_block(show):
    # an empty block still renders both braces
    assert show("{}") == lox("""
        {
        }
    """)
    assert show("{ print 1; }") == lox("""
        {
          print 1;
        }
    """)
    assert show("{ { print 1; } }") == lox("""
        {
          {
            print 1;
          }
        }
    """)


def test_expression_statement(show):
    assert show("1 + 2;") == "1 + 2;"
    assert show("f();") == "f();"


def test_statements_are_one_per_line(show):
    # successive statements each get their own line, with no blank between
    assert show("var a = 1; var b = 2; print a;") == lox("""
        var a = 1;
        var b = 2;
        print a;
    """)


# --- expressions ---


def test_assign(show_expr):
    assert show_expr("x = 1;") == "x = 1"
    # assignment is right-associative, and chains render flat
    assert show_expr("x = y = 1;") == "x = y = 1"


def test_set(show_expr):
    assert show_expr("o.x = 1;") == "o.x = 1"
    # the object is an arbitrary expression, so a chained target renders as one
    assert show_expr("a.b.c = 2;") == "a.b.c = 2"
    assert show_expr("this.x = 1 + 2;") == "this.x = 1 + 2"


def test_conditional(show_expr):
    assert show_expr("a ? b : c;") == "a ? b : c"
    # the else branch is itself a conditional, so a chain renders flat
    assert show_expr("a ? b : c ? d : e;") == "a ? b : c ? d : e"


def test_logical(show_expr):
    assert show_expr("a and b;") == "a and b"
    assert show_expr("a or b;") == "a or b"
    assert show_expr("a and b or c;") == "a and b or c"


def test_binary(show_expr):
    assert show_expr("1 + 2;") == "1 + 2"
    assert show_expr("a == b;") == "a == b"
    # no parentheses are added: precedence is already in the tree's shape, and
    # the flat rendering re-parses to the same tree
    assert show_expr("1 + 2 * 3;") == "1 + 2 * 3"


def test_unary(show_expr):
    # the operator binds tight, with no space before its operand
    assert show_expr("-a;") == "-a"
    assert show_expr("!true;") == "!true"
    # stacked unaries stay unspaced -- Lox has no `--` token, so this re-scans
    # as two minus signs rather than one operator
    assert show_expr("- -1;") == "--1"
    assert show_expr("1 - -1;") == "1 - -1"


def test_call(show_expr):
    assert show_expr("f();") == "f()"
    assert show_expr("f(1);") == "f(1)"
    assert show_expr("f(1, 2);") == "f(1, 2)"
    # the callee is an arbitrary expression, so calls chain
    assert show_expr("f(1)(2);") == "f(1)(2)"
    assert show_expr("o.m(1);") == "o.m(1)"


def test_get(show_expr):
    assert show_expr("o.x;") == "o.x"
    assert show_expr("a.b.c;") == "a.b.c"
    assert show_expr("f().x;") == "f().x"


@pytest.mark.parametrize(
    "source, expected",
    [
        ("123;", "123"),
        ("45.67;", "45.67"),
        ('"hello";', '"hello"'),
        ("true;", "true"),
        ("false;", "false"),
        ("nil;", "nil"),
    ],
)
def test_literal(show_expr, source, expected):
    assert show_expr(source) == expected


def test_small_number_literal_is_positional(show_expr):
    # Lox numbers have no exponent syntax, so a magnitude Python would render
    # as 1e-07 has to come out in full or the output stops being Lox
    assert show_expr("0.0000001;") == "0.0000001"


def test_string_literal_may_span_lines(show):
    # the scanner accepts a newline inside a string, and the printer emits it
    # as-is: the literal keeps its own line breaks and its own indentation
    assert show('var s = "a\nb";') == 'var s = "a\nb";'


def test_super(show_expr):
    # `super` is never an expression by itself; the method access comes with it
    assert show_expr("super.m;") == "super.m"
    assert show_expr("super.m(1);") == "super.m(1)"


def test_this(show_expr):
    assert show_expr("this;") == "this"
    assert show_expr("this.x;") == "this.x"


def test_variable(show_expr):
    assert show_expr("x;") == "x"
    assert show_expr("foo;") == "foo"


def test_grouping(show_expr):
    # parentheses survive because the parser kept them as a Grouping node --
    # this is what makes the flat binary rendering safe
    assert show_expr("(1);") == "(1)"
    assert show_expr("(1 + 2) * 3;") == "(1 + 2) * 3"
    assert show_expr("((1));") == "((1))"


# --- layout ---


def test_indentation_is_two_spaces_per_level(show):
    assert show("fun f() { if (a) { while (b) { print 1; } } }") == lox("""
        fun f() {
          if (a) {
            while (b) {
              print 1;
            }
          }
        }
    """)


def test_indentation_past_the_prefix_cache(show):
    # the printer memoises indent prefixes for the first 20 levels and builds
    # deeper ones on demand; the two paths must agree
    depth = 30
    source = "{" * depth + " print 1; " + "}" * depth
    lines = show(source).split("\n")

    assert len(lines) == 2 * depth + 1
    for level, line in enumerate(lines[:depth]):
        assert line == "  " * level + "{"
    assert lines[depth] == "  " * depth + "print 1;"
    for level, line in enumerate(reversed(lines[depth + 1 :])):
        assert line == "  " * level + "}"


def test_no_trailing_newline(show):
    # the printer returns a block of text, not a file: joining or embedding it
    # is the caller's business
    assert show("print 1;") == "print 1;"
    assert show("{ print 1; }").endswith("}")


# --- round trip ---


_ROUND_TRIP_SOURCES = (
    "",
    "print 1;",
    "print a, b, c;",
    'print "hi", 1, true, nil, 45.67;',
    "print 0.0000001;",
    'var s = "a\nb";',
    "var x;",
    "var x = 1;",
    "x = 1;",
    "x = y = 1;",
    "1 + 2 * 3;",
    "(1 + 2) * 3;",
    "((1 + 2)) * 3;",
    "- -1;",
    "1 - -1;",
    "!!true;",
    "a ? b : c;",
    "a ? b : c ? d : e;",
    "a and b or c;",
    "{}",
    "{ { print 1; } }",
    "if (a) print 1;",
    "if (a) print 1; else print 2;",
    "if (a) { print 1; } else { print 2; }",
    "if (a) if (b) print 1; else print 2;",
    "if (a) { if (b) print 1; } else print 2;",
    "if (a) print 1; else if (b) print 2; else print 3;",
    "while (a) b;",
    "while (a) { break; continue; }",
    "for (;;) break;",
    "for (var i = 0; i < 3; i = i + 1) print i;",
    "for (i = 0; ; ) { print i; }",
    "for (var i = 0; ; i = i + 1) print i;",
    "fun f() {}",
    "fun f(a, b) { return a + b; }",
    "fun f() { return; }",
    "fun outer() { fun inner() { print 1; } return inner; }",
    "f();",
    "f(1, 2);",
    "f(1)(2);",
    "o.x;",
    "o.x = 1;",
    "a.b.c = 2;",
    "class A {}",
    "class A { m() {} }",
    "class B < A { init(x) { this.x = x; } m(a) { return super.m(a); } }",
    "var a = 1; var b = 2; print a + b;",
)


@pytest.mark.parametrize("source", _ROUND_TRIP_SOURCES)
def test_round_trip(show, parse_program, source):
    # the point of the printer: what it emits parses back to the same tree.
    # Token equality ignores position, so only the structure is compared --
    # which is right, since the printer reflows the source
    assert parse_program(show(source)) == parse_program(source)


@pytest.mark.parametrize("source", _ROUND_TRIP_SOURCES)
def test_round_trip_is_idempotent(show, source):
    # printing already-printed source changes nothing, so the output is a
    # fixed point and not merely equivalent
    assert show(show(source)) == show(source)


# --- entry points ---


def test_print_accepts_a_program(show, parse_program):
    program = parse_program("print 1; print 2;")
    assert CodePrinter().print(program) == lox("""
        print 1;
        print 2;
    """)


def test_print_accepts_a_single_statement(parse_program):
    (statement,) = parse_program("{ print 1; }").statements
    assert CodePrinter().print(statement) == lox("""
        {
          print 1;
        }
    """)


def test_print_accepts_a_single_expression():
    # an Expr is rendered without a terminating `;` -- it is not a statement
    assert CodePrinter().print(Literal(1.0)) == "1"


def test_empty_program(show):
    # a source with no statements prints as nothing at all, not a blank line
    assert show("") == ""
    assert show("// just a comment") == ""


def test_printer_can_be_reused(parse_program):
    # one printer drives several sources, in either entry point, without
    # carrying state between them
    printer = CodePrinter()
    program = parse_program("print 1; print 2;")
    expected = lox("""
        print 1;
        print 2;
    """)

    assert printer.print(program) == expected
    assert printer.print(Literal(3.0)) == "3"
    assert printer.print(parse_program("{ print 4; }")) == lox("""
        {
          print 4;
        }
    """)
    # the Expr path returns early, so it must not leave the line buffer dirty
    assert printer.print(program) == expected
