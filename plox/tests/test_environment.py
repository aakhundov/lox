import pytest

from plox.common import Token, TokenType as TT, InterpreterError
from plox.environment import Environment


def name(lexeme, line=1, col=1):
    """Build an identifier token carrying a source position.

    `Environment` looks up variables by the token's lexeme and, on failure,
    raises an `InterpreterError` located at the token, so the position matters
    for the error-location assertions.
    """
    return Token(TT.IDENTIFIER, lexeme, None, line_num=line, col_num=col)


def test_define_and_get():
    env = Environment()
    env.define("x", 1.0)
    assert env.get(name("x")) == 1.0


def test_define_defaults_are_stored_verbatim():
    # define is the raw storage primitive; nil is a value like any other
    env = Environment()
    env.define("x", None)
    assert env.get(name("x")) is None


def test_redefine_overwrites_in_same_scope():
    env = Environment()
    env.define("x", 1.0)
    env.define("x", 2.0)
    assert env.get(name("x")) == 2.0


def test_get_undefined_raises_at_token():
    env = Environment()
    with pytest.raises(InterpreterError) as excinfo:
        env.get(name("x", line=3, col=7))
    assert str(excinfo.value) == "Undefined variable: x"
    assert excinfo.value.get_line_info() == (3, 7)


def test_assign_updates_existing():
    env = Environment()
    env.define("x", 1.0)
    env.assign(name("x"), 2.0)
    assert env.get(name("x")) == 2.0


def test_assign_undefined_raises_at_token():
    env = Environment()
    with pytest.raises(InterpreterError) as excinfo:
        env.assign(name("x", line=4, col=2), 1.0)
    assert str(excinfo.value) == "Undefined variable: x"
    assert excinfo.value.get_line_info() == (4, 2)


def test_get_walks_up_to_enclosing_scope():
    parent = Environment()
    parent.define("x", 1.0)
    child = Environment(parent=parent)
    assert child.get(name("x")) == 1.0


def test_inner_define_shadows_without_touching_parent():
    parent = Environment()
    parent.define("x", 1.0)
    child = Environment(parent=parent)
    child.define("x", 2.0)
    # the child sees its own binding; the parent's is untouched
    assert child.get(name("x")) == 2.0
    assert parent.get(name("x")) == 1.0


def test_assign_reaches_enclosing_scope():
    parent = Environment()
    parent.define("x", 1.0)
    child = Environment(parent=parent)
    # no local binding, so the assignment walks up and mutates the parent's
    child.assign(name("x"), 2.0)
    assert parent.get(name("x")) == 2.0


def test_assign_prefers_nearest_binding():
    parent = Environment()
    parent.define("x", 1.0)
    child = Environment(parent=parent)
    child.define("x", 2.0)
    child.assign(name("x"), 3.0)
    # the shadowing local is updated; the parent's binding is left alone
    assert child.get(name("x")) == 3.0
    assert parent.get(name("x")) == 1.0


def test_parent_cannot_see_child_binding():
    parent = Environment()
    child = Environment(parent=parent)
    child.define("x", 1.0)
    with pytest.raises(InterpreterError):
        parent.get(name("x"))
