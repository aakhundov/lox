import dataclasses

import pytest

from plox.ast import Assign, Expr, Stmt, Super, This, Variable
from plox.resolver import Resolver, ResolverError

# the node types the resolver writes a scope distance onto
_RESOLVED = (Variable, Assign, This, Super)

# the ones naming themselves with a keyword rather than an identifier
_KEYWORD_NAMED = (This, Super)


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


def _resolved_name(node):
    """Give the name a resolved node was looked up under.

    `this` and `super` are keywords rather than identifiers, so `This`/`Super`
    carry their lexeme as `keyword` where `Variable`/`Assign` carry theirs as
    `name`. They resolve through the same scope machinery either way, which is
    the point. `Super` also holds the method name, but that is looked up on the
    superclass at runtime, so it has no scope distance and does not appear.
    """
    return (node.keyword if isinstance(node, _KEYWORD_NAMED) else node.name).lexeme


@pytest.fixture
def resolve(resolve_program):
    """Return a helper that resolves `source` into a tuple of Stmt.

    The pipeline hands back a `Program`; the tests walk the statements in it,
    so the helper unwraps it.
    """

    def _resolve(source):
        return resolve_program(source).statements

    return _resolve


@pytest.fixture
def distances(resolve):
    """Return a helper listing each resolved name and its scope distance.

    Distances are what the resolver produces: they are written onto the
    `Variable`, `Assign`, `This` and `Super` nodes themselves, so the helper
    walks the resolved tree and pairs every such node's lexeme with its
    distance, in traversal order. `None` means the name was not found in any
    enclosing local scope and is therefore assumed global. Declarations
    (`var a = 1;`, `fun f() {}`, `class A {}`) are not uses, so they do not
    appear -- only *uses* of a name do.
    """

    def _distances(source):
        return [
            (_resolved_name(node), node.get_distance())
            for statement in resolve(source)
            for node in _walk(statement)
            if isinstance(node, _RESOLVED)
        ]

    return _distances


@pytest.fixture
def resolve_errors(collect_errors, resolve):
    """Return a helper that resolves `source` expecting failure.

    The resolver keeps going after each error it finds, so a single source can
    yield several.
    """

    def _resolve_errors(source):
        return collect_errors(ResolverError, resolve, source)

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
        # a method body sits inside an implicit scope holding `this`, so the
        # parameter scope is distance 0 and `this` is one hop further out
        ("class A { m() { print this; } }", [("this", 1)]),
        ("class A { m(a) { print a; } }", [("a", 0)]),
        # a block inside the body adds a hop to both
        ("class A { m() { { print this; } } }", [("this", 2)]),
        # the `this` scope wraps the whole class, but each method still gets
        # its own parameter scope, so the distance is the same in every one
        (
            "class A { m() { print this; } n() { print this; } }",
            [("this", 1), ("this", 1)],
        ),
        ("class A { m(a) { print a; } n(b) { print b; } }", [("a", 0), ("b", 0)]),
        # a function declared inside a method closes over `this`, one hop
        # further out again -- this is why a callback can still see its object
        ("class A { m() { fun f() { print this; } } }", [("this", 2)]),
        # a field access resolves the object, not the property name: `x` is
        # looked up on the instance at runtime, so it has no scope distance
        ("class A { m() { print this.x; } }", [("this", 1)]),
        ("class A { init() { this.x = 1; } }", [("this", 1)]),
    ],
)
def test_this_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a subclass opens one scope more than a base class does: the `super`
        # binding sits just outside the `this` binding, so from a method body
        # `super` is always one step further out than `this`
        ("class B < A { m() { super.m(); } }", [("A", None), ("super", 2)]),
        (
            "class B < A { m() { print this; super.m(); } }",
            [("A", None), ("this", 1), ("super", 2)],
        ),
        # parameters share the body's scope, so they shift nothing
        ("class B < A { m(a) { super.m(a); } }", [("A", None), ("super", 2), ("a", 0)]),
        # a block inside the method adds a scope, pushing `super` further out
        ("class B < A { m() { { super.m(); } } }", [("A", None), ("super", 3)]),
        # a function declared in a method closes over `super` the same way it
        # closes over `this`, so a callback can still reach the superclass
        ("class B < A { m() { fun f() { super.m(); } } }", [("A", None), ("super", 3)]),
        # the method name after the dot is looked up on the superclass at
        # runtime, so only the keyword carries a distance -- `m` never appears
        ("class B < A { m() { super.other(); } }", [("A", None), ("super", 2)]),
        # a nested subclass binds its own `super`, and the inner one wins
        (
            "class B < A { m() { class C < A { n() { super.n(); } } } }",
            [("A", None), ("A", None), ("super", 2)],
        ),
    ],
)
def test_super_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a class declared at the top level is a global like any other name
        ("class A {} var a = A();", [("A", None)]),
        # ...and a local one resolves to the scope it was declared in
        ("{ class A {} var a = A(); }", [("A", 0)]),
        # a method may name its own class: the declaration is complete before
        # any method body runs, and it sits outside the `this` scope
        ("{ class A { m() { return A; } } }", [("A", 2)]),
        # an enclosing local reached from a method body crosses the parameter
        # scope and the `this` scope
        ("{ var g = 1; class A { m() { print g; } } }", [("g", 2)]),
        # a global stays global however deep the method is
        ("var g = 1; class A { m() { print g; } }", [("g", None)]),
        # a parameter shadows an enclosing local of the same name
        ("{ var a = 1; class A { m(a) { print a; } } }", [("a", 0)]),
    ],
)
def test_class_name_distance(distances, source, expected):
    assert distances(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the superclass name is resolved where the class is declared, before
        # any of the class's own scopes are opened -- it is an ordinary use
        ("class A {} class B < A {}", [("A", None)]),
        ("{ class A {} class B < A {} }", [("A", 0)]),
        ("{ class A {} { class B < A {} } }", [("A", 1)]),
        # ...so it is looked up like any other name, and a local shadows
        ("{ class A {} { class A {} class B < A {} } }", [("A", 0)]),
        # reaching an enclosing local from a method body of a *subclass* costs
        # one step more than from a base class, because of the `super` scope
        (
            "{ var g = 1; class A {} class B < A { m() { print g; } } }",
            [("A", 0), ("g", 3)],
        ),
        # a method may name its own superclass, crossing the same scopes
        ("{ class A {} class B < A { m() { return A; } } }", [("A", 0), ("A", 3)]),
    ],
)
def test_superclass_name_distance(distances, source, expected):
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
def test_self_reference_in_initializer_error(
    resolve_errors, error_position, source, position
):
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
def test_duplicate_declaration_error(resolve_errors, error_position, source, position):
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
def test_return_outside_function_error(
    resolve_errors, error_position, source, position
):
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
    "source, position",
    [
        # `this` only means something inside a method body, so it is rejected
        # anywhere else, at the keyword itself
        ("print this;", (1, 7)),
        ("this;", (1, 1)),
        ("{ print this; }", (1, 9)),
        # a plain function is not a method, however it is later called
        ("fun f() { print this; }", (1, 17)),
        # a function declared inside a method is fine (it closes over `this`),
        # but one declared outside is not, even next to a class
        ("class A {} fun f() { return this; }", (1, 29)),
        # accessing a property of `this` is still a use of `this`
        ("print this.x;", (1, 7)),
        ("this.x = 1;", (1, 1)),
    ],
)
def test_this_outside_class_error(resolve_errors, error_position, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "Can't use 'this' outside of class"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        "class A { m() { return this; } }",
        # `this` is in scope for the whole class, not just one method
        "class A { init() { this.x = 1; } get_() { return this.x; } }",
        # a function declared inside a method closes over the method's `this`
        "class A { m() { fun f() { return this; } } }",
        # so does a nested class's method, over its own `this`
        "class A { m() { class B { n() { return this; } } } }",
        # `this` may appear anywhere an expression may
        "class A { m() { if (this) print this; while (false) print this; } }",
    ],
)
def test_this_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, position",
    [
        # a class cannot be its own superclass: the name is already declared
        # when the superclass is resolved, so this would otherwise inherit from
        # the half-built class itself
        ("class A < A {}", (1, 11)),
        ("{ class A < A {} }", (1, 13)),
        # the check compares names, so a nested class shadowing an outer one of
        # the same name is caught too
        ("class A { m() { class B < B {} } }", (1, 27)),
    ],
)
def test_inherit_from_itself_error(resolve_errors, error_position, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "A class can't inherit from itself"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # `super` needs a surrounding class to have a superclass at all, so
        # outside any class it is rejected at the keyword
        ("super.m();", (1, 1)),
        ("print super.m();", (1, 7)),
        ("{ super.m(); }", (1, 3)),
        # a plain function is not a method, however it is later called
        ("fun f() { super.m(); }", (1, 11)),
        ("class A {} fun f() { super.m(); }", (1, 22)),
    ],
)
def test_super_outside_class_error(resolve_errors, error_position, source, position):
    (error,) = resolve_errors(source)
    assert str(error) == "Can't use super outside of class"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        # inside a class is not enough -- there has to be a superclass to
        # reach, which is a different error from being outside a class
        ("class A { m() { super.m(); } }", (1, 17)),
        ("class A { init() { super.init(); } }", (1, 20)),
        # the innermost class decides: a base class nested inside a subclass's
        # method does not inherit its enclosing class's superclass
        ("class B < A { m() { class C { n() { super.n(); } } } }", (1, 37)),
    ],
)
def test_super_without_superclass_error(
    resolve_errors, error_position, source, position
):
    (error,) = resolve_errors(source)
    assert str(error) == "Can't use super in class with no superclass"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        "class B < A { m() { super.m(); } }",
        # `super` is in scope for the whole class, not just one method
        "class B < A { init() { super.init(); } m() { return super.m(); } }",
        # reading the method without calling it is a use like any other
        "class B < A { m() { var f = super.m; } }",
        # a function declared inside a method closes over the method's `super`
        "class B < A { m() { fun f() { super.m(); } } }",
        # so does a nested subclass's method, over its own `super`
        "class B < A { m() { class C < A { n() { super.n(); } } } }",
        # the innermost class decides here too: leaving a nested base class
        # puts the enclosing subclass's superclass back in reach
        "class B < A { m() { class C {} super.m(); } }",
        # `super` may appear anywhere an expression may
        "class B < A { m() { if (super.m()) while (false) print super.m(); } }",
    ],
)
def test_super_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, position",
    [
        # an initializer implicitly returns the new instance, so returning
        # something else would silently do nothing -- reject it at the keyword
        ("class A { init() { return 1; } }", (1, 20)),
        ("class A { init() { return this; } }", (1, 20)),
        # wherever in the initializer it appears
        ("class A { init() { if (true) return 1; } }", (1, 30)),
        ("class A { init() { while (true) return 1; } }", (1, 33)),
        ("class A { init() { { return 1; } } }", (1, 22)),
    ],
)
def test_return_value_from_initializer_error(
    resolve_errors, error_position, source, position
):
    (error,) = resolve_errors(source)
    assert str(error) == "Can't return value from initializer"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        # a bare `return` is how an initializer exits early, and is allowed
        "class A { init() { return; } }",
        "class A { init() { if (true) return; this.x = 1; } }",
        # only `init` is an initializer; any other method returns normally
        "class A { m() { return 1; } }",
        # a function declared inside an initializer is not itself one
        "class A { init() { fun f() { return 1; } } }",
        # `init` is only special as a method name, not as a function's
        "fun init() { return 1; }",
        "class A { m() { fun init() { return 1; } } }",
    ],
)
def test_return_in_initializer_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, message, position",
    [
        # a method body is a function body: `return` is licensed inside it...
        ("class A { m() { break; } }", "break allowed only inside loop body", (1, 17)),
        # ...and it starts a fresh loop context, so an enclosing loop does not
        # license a jump inside a method declared within it
        (
            "while (true) { class A { m() { break; } } }",
            "break allowed only inside loop body",
            (1, 32),
        ),
        (
            "for (;;) { class A { m() { continue; } } }",
            "continue allowed only inside loop body",
            (1, 28),
        ),
    ],
)
def test_method_body_is_a_function_context(
    resolve_errors, error_position, source, message, position
):
    (error,) = resolve_errors(source)
    assert str(error) == message
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        # `return` needs a function body, and a method body is one
        "class A { m() { return 1; } }",
        # a loop inside a method licenses jumps normally
        "class A { m() { while (true) break; } }",
        # the enclosing loop context is restored after the class declaration
        "while (true) { class A { m() {} } break; }",
    ],
)
def test_method_body_allowed(resolve, source):
    resolve(source)  # must not raise


@pytest.mark.parametrize(
    "source, position",
    [
        # a class binds its name in the enclosing scope, so it collides there
        # exactly like a `var` or a `fun` does
        ("{ class A {} class A {} }", (1, 20)),
        ("{ var A = 1; class A {} }", (1, 20)),
        ("{ class A {} fun A() {} }", (1, 18)),
        ("fun f(A) { class A {} }", (1, 18)),
    ],
)
def test_duplicate_class_declaration_error(
    resolve_errors, error_position, source, position
):
    (error,) = resolve_errors(source)
    assert str(error) == "Already a variable with this name in this scope"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source",
    [
        # redeclaration is allowed at the top level, as for every declaration
        "class A {} class A {}",
        # sibling and nested scopes are separate
        "{ class A {} } { class A {} }",
        "{ class A {} { class A {} } }",
        # a method's parameters are its own scope, so they may shadow the class
        "{ class A { m(A) { return A; } } }",
    ],
)
def test_class_redeclaration_allowed(resolve, source):
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
def test_loop_jump_outside_loop_error(
    resolve_errors, error_position, source, message, position
):
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


def test_multiple_errors(resolve_errors, error_position):
    # every error is collected in one pass, in source order, rather than
    # reporting only the first one
    errors = resolve_errors("{ var a = 1; var a = 2; var b = b; }")
    assert [str(e) for e in errors] == [
        "Already a variable with this name in this scope",
        "Can't read local variable in its own initializer",
    ]
    assert [error_position(e) for e in errors] == [(1, 18), (1, 33)]


def test_resolver_can_be_reused(parse_program):
    # `resolve` clears its scope stack and collected errors per call, so a
    # failed resolution does not leak into the next one. The one Resolver is
    # driven by hand here: the `resolve` fixture makes a fresh one per call.
    resolver = Resolver()
    with pytest.raises(ExceptionGroup):
        resolver.resolve(parse_program("{ var a = 1; var a = 2; }"))

    program = parse_program("{ var b = 1; print b; }")
    resolver.resolve(program)  # must not raise
    assert [
        node.get_distance()
        for statement in program.statements
        for node in _walk(statement)
        if isinstance(node, Variable)
    ] == [0]
