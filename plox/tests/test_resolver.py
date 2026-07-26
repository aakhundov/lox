import dataclasses

import pytest

from plox.ast import Assign, Expr, Stmt, Variable
from plox.parser import Parser
from plox.resolver import Resolver, ResolverError
from plox.scanner import Scanner


def _walk(node):
    """Yield `node` and every Expr/Stmt below it, parents before children.

    The AST nodes are dataclasses, so their children are reachable generically:
    a field either holds a node, a list of nodes, or something that is neither
    (a Token, a literal value, the resolver's own `_meta` dict) and is skipped.
    """
    yield node
    for field in dataclasses.fields(node):
        value = getattr(node, field.name)
        for item in value if isinstance(value, list) else [value]:
            if isinstance(item, (Expr, Stmt)):
                yield from _walk(item)


@pytest.fixture
def resolve():
    """Return a helper that scans, parses and resolves `source`.

    Driving the resolver through the real Scanner and Parser mirrors the actual
    pipeline and keeps expectations free of hand-built AST details.
    """

    def _resolve(source):
        program = Parser(Scanner(source).scan()).parse()
        Resolver().resolve(program)
        return program.statements

    return _resolve


@pytest.fixture
def distances(resolve):
    """Return a helper listing each resolved name and its scope distance.

    Distances are what the resolver produces: they are written onto the
    `Variable` and `Assign` nodes themselves, so the helper walks the resolved
    tree and pairs every such node's lexeme with its distance, in traversal
    order. `None` means the name was not found in any enclosing local scope and
    is therefore assumed global. Declarations (`var a = 1;`, `fun f() {}`) are
    not `Variable` nodes and so do not appear -- only *uses* of a name do.
    """

    def _distances(source):
        return [
            (node.name.lexeme, node.get_distance())
            for statement in resolve(source)
            for node in _walk(statement)
            if isinstance(node, (Variable, Assign))
        ]

    return _distances


def error_position(error):
    """Return the single source position `error` points at.

    `get_line_info` yields one (line, col) pair per reported position -- the
    interpreter reports a whole call stack that way. A resolver error has no
    stack behind it, so the unpack doubles as a check that there is exactly one.
    """
    (position,) = error.get_line_info()
    return position


@pytest.fixture
def resolve_errors(resolve):
    """Return a helper that resolves `source` expecting failure.

    The resolver reports every error it finds by raising a single
    ExceptionGroup; this unwraps it into the flat list of ResolverErrors, in
    source order.
    """

    def _resolve_errors(source):
        with pytest.raises(ExceptionGroup) as excinfo:
            resolve(source)

        errors: list[ResolverError] = []
        for error in excinfo.value.exceptions:
            assert isinstance(error, ResolverError)  # flat: no nested groups
            errors.append(error)
        return errors

    return _resolve_errors


@pytest.mark.parametrize(
    "source, expected",
    [
        # a top-level name lives in globals, which is not a resolved scope
        ("var a = 1; print a;", [("a", None)]),
        ("var a = 1; a = 2;", [("a", None)]),
        # a global stays global when used from inside a function body
        ("var a = 1; fun f() { print a; }", [("a", None)]),
        # ...and so does the function's own name at the call site
        ("fun f() {} f();", [("f", None)]),
        # an undeclared name is a global too: the resolver never reports it,
        # the interpreter raises "Undefined variable" when it runs
        ("print a;", [("a", None)]),
    ],
)
def test_global_names_are_unresolved(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the block the name is declared in is distance 0
        ("{ var a = 1; print a; }", [("a", 0)]),
        # each enclosing block adds one hop
        ("{ var a = 1; { print a; } }", [("a", 1)]),
        ("{ var a = 1; { { print a; } } }", [("a", 2)]),
        # a name declared in an inner block is not visible to the outer one,
        # so the outer use falls through to globals
        ("{ { var a = 1; } print a; }", [("a", None)]),
    ],
)
def test_block_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # parameters and the body share one scope, so a parameter is distance 0
        ("fun f(a) { print a; }", [("a", 0)]),
        # a block inside the body adds a hop
        ("fun f(a) { { print a; } }", [("a", 1)]),
        # a local declared in the body sits in that same function scope
        ("fun f() { var a = 1; print a; }", [("a", 0)]),
        # a nested function's own scope is one more hop out to the enclosing
        # function's local -- this is what closures capture
        ("fun mk() { var c = 1; fun get() { return c; } }", [("c", 1)]),
    ],
)
def test_function_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # assignment resolves exactly like a read
        ("{ var a = 1; a = 2; }", [("a", 0)]),
        ("{ var a = 1; { a = 2; } }", [("a", 1)]),
        # both sides of `a = a + 1` resolve: the target, then the read
        ("{ var a = 1; a = a + 1; }", [("a", 0), ("a", 0)]),
    ],
)
def test_assignment_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the innermost declaration wins
        ("{ var a = 1; { var a = 2; print a; } }", [("a", 0)]),
        # a parameter shadows an outer local of the same name
        ("{ var a = 1; fun f(a) { print a; } }", [("a", 0)]),
        # ...and the outer local is still reachable from where it is in scope
        ("{ var a = 1; { var a = 2; } print a; }", [("a", 0)]),
    ],
)
def test_shadowing_resolves_to_nearest(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a `var` initializer gets its own scope around the whole loop, so the
        # condition, the increment and a non-block body all sit at distance 0
        (
            "for (var i = 0; i < 1; i = i + 1) print i;",
            [("i", 0), ("i", 0), ("i", 0), ("i", 0)],
        ),
        # a block body is one hop further out
        (
            "for (var i = 0; i < 1; i = i + 1) { print i; }",
            [("i", 0), ("i", 0), ("i", 0), ("i", 1)],
        ),
        # without a `var` initializer there is no loop scope at all
        ("var i = 0; for (; i < 1; i = i + 1) print i;", [("i", None)] * 4),
    ],
)
def test_for_loop_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, position",
    [
        # reading the variable being declared, inside its own initializer
        ("{ var a = a; }", (1, 11)),
        ("fun f() { var a = a; }", (1, 19)),
        # the same name nested in a larger initializer expression
        ("{ var a = 1 + a; }", (1, 15)),
        # ...even when an outer binding of that name exists: the inner
        # declaration already shadows it, so the read is still the local one
        ("{ var a = 1; { var a = a; } }", (1, 24)),
        # the check is on the name, not on how it is used: calling an outer
        # function of the same name from the initializer is rejected too
        ("fun a() {} { var a = a(); }", (1, 22)),
    ],
)
def test_self_reference_in_initializer_error(resolve_errors, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "Can't read local variable in its own initializer"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        # globals are not tracked in scopes, so this is not a resolver error
        # (it is a runtime one: `a` is undefined when the initializer runs)
        "var a = a;",
        # the initializer reads an outer binding under a different name
        "var a = 1; { var b = a; }",
        # a use of the name after the declaration is complete is fine
        "{ var a = 1; var b = a; }",
    ],
)
def test_self_reference_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, position",
    [
        # two `var`s of the same name in one block
        ("{ var a = 1; var a = 2; }", (1, 18)),
        # a parameter list is a scope like any other
        ("fun f(a, a) {}", (1, 10)),
        # function declarations bind a name too, and so collide
        ("{ fun g() {} fun g() {} }", (1, 18)),
        ("{ var a = 1; fun a() {} }", (1, 18)),
        # a parameter collides with a body-level declaration of the same name,
        # since the body shares the parameter scope
        ("fun f(a) { var a = 1; }", (1, 16)),
    ],
)
def test_duplicate_declaration_error(resolve_errors, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "Already a variable with this name in this scope"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        # redeclaration is allowed at the top level, where the REPL relies on it
        "var a = 1; var a = 2;",
        "fun f() {} fun f() {}",
        # sibling blocks are separate scopes
        "{ var a = 1; } { var a = 2; }",
        # so are nested ones: the inner declaration shadows, it does not clash
        "{ var a = 1; { var a = 2; } }",
        # a parameter may shadow a global or an enclosing local
        "var a = 1; fun f(a) { return a; }",
        "{ var a = 1; fun f(a) { return a; } }",
        # each loop has its own scope for its `var` initializer
        "for (var i = 0; i < 1; i = i + 1) {} for (var i = 0; i < 1; i = i + 1) {}",
        # a loop body block is nested inside the initializer's scope
        "for (var i = 0; i < 1; i = i + 1) { var i = 2; }",
    ],
)
def test_redeclaration_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, position",
    [
        # `return` outside any function body is rejected, at the keyword itself
        ("return;", (1, 1)),
        ("return 1;", (1, 1)),
        # a loop is not a function body
        ("while (true) return 1;", (1, 14)),
    ],
)
def test_return_outside_function_error(resolve_errors, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "return allowed only inside function body"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        "fun f() { return; }",
        # each function body is its own context, and the inner one ends with it
        "fun f() { fun g() { return 1; } return 2; }",
        # a loop inside the body does not displace the enclosing function
        "fun f() { while (true) { return 1; } }",
    ],
)
def test_return_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, message, position",
    [
        # a jump outside any loop body is rejected, at the keyword itself
        ("break;", "break allowed only inside loop body", (1, 1)),
        ("continue;", "continue allowed only inside loop body", (1, 1)),
        # an enclosing `if` is not a loop
        ("if (true) break;", "break allowed only inside loop body", (1, 11)),
        # the loop has already ended by the time the jump is reached
        ("while (true) {} break;", "break allowed only inside loop body", (1, 17)),
        # a function body starts a fresh loop context: an enclosing loop does
        # not license a jump inside a function declared within it, because at
        # runtime the jump would unwind out of the call into the caller's loop
        (
            "while (true) { fun f() { break; } }",
            "break allowed only inside loop body",
            (1, 26),
        ),
        (
            "for (;;) { fun f() { continue; } }",
            "continue allowed only inside loop body",
            (1, 22),
        ),
        # the same holds without any enclosing loop at all
        ("fun f() { break; }", "break allowed only inside loop body", (1, 11)),
    ],
)
def test_loop_jump_outside_loop_error(resolve_errors, source, message, position):
    (error,) = resolve_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        "while (true) break;",
        "for (;;) continue;",
        # the enclosing loop context is restored after the function body, so a
        # jump following the declaration is still valid
        "while (true) { fun f() { var x = 1; } break; }",
        # a loop inside a function body licenses jumps normally
        "fun f() { for (;;) continue; }",
    ],
)
def test_loop_jump_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize("count", [Resolver._MAX_ARITY, Resolver._MAX_ARITY + 1])
def test_max_parameters(resolve, resolve_errors, count):
    # the limit is on the count itself, so exactly _MAX_ARITY is still legal
    params = ", ".join(f"p{i}" for i in range(count))
    source = f"fun f({params}) {{}}"
    if count <= Resolver._MAX_ARITY:
        assert resolve(source)
        return
    # over the limit reports once per function, not once per excess parameter
    (error,) = resolve_errors(source)
    assert str(error) == f"Max {Resolver._MAX_ARITY} parameters allowed"


@pytest.mark.parametrize("count", [Resolver._MAX_ARITY, Resolver._MAX_ARITY + 1])
def test_max_arguments(resolve, resolve_errors, count):
    args = ", ".join(str(i) for i in range(count))
    source = f"f({args});"
    if count <= Resolver._MAX_ARITY:
        assert resolve(source)
        return
    # likewise reported once per call site
    (error,) = resolve_errors(source)
    assert str(error) == f"Max {Resolver._MAX_ARITY} arguments allowed"


def test_multiple_errors(resolve_errors):
    # every error is collected in one pass, in source order, rather than
    # reporting only the first one
    errors = resolve_errors("{ var a = 1; var a = 2; var b = b; }")
    assert [str(e) for e in errors] == [
        "Already a variable with this name in this scope",
        "Can't read local variable in its own initializer",
    ]
    assert [error_position(e) for e in errors] == [(1, 18), (1, 33)]


def test_resolver_can_be_reused(resolve):
    # `resolve` clears its scope stack and collected errors per call, so a
    # failed resolution does not leak into the next one
    resolver = Resolver()
    with pytest.raises(ExceptionGroup):
        resolver.resolve(Parser(Scanner("{ var a = 1; var a = 2; }").scan()).parse())

    program = Parser(Scanner("{ var b = 1; print b; }").scan()).parse()
    resolver.resolve(program)  # must not raise
    assert [
        node.get_distance()
        for statement in program.statements
        for node in _walk(statement)
        if isinstance(node, Variable)
    ] == [0]
