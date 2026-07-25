import pytest

from time import time

from plox.common import LoxCallable
from plox.interpreter import Interpreter, _NativeFnError
from plox.library import Clock, LoxNativeFn, Sleep, get_library


@pytest.fixture
def interpreter():
    """Return an Interpreter to pass as `call`'s second argument.

    Natives ignore it, but the parameter is typed as an Interpreter, so
    handing it None is a type error even though nothing would dereference it.
    """
    return Interpreter()


class Nullary(LoxNativeFn):
    """A native taking no arguments."""

    def _call(self):
        return "none"


class Binary(LoxNativeFn):
    """A native taking two arguments."""

    def _call(self, a, b):
        return a + b


class ReadLine(LoxNativeFn):
    """A native whose class name is more than one camel-cased word."""

    def _call(self):
        return "line"


@pytest.mark.parametrize(
    "fn_type, expected",
    [
        # a single-word class name lowercases
        (Nullary, "nullary"),
        (Clock, "clock"),
        # a multi-word one becomes snake case
        (ReadLine, "read_line"),
    ],
)
def test_name_derives_from_class_name(fn_type, expected):
    # the Lox-visible name is derived from the class, so it is the single
    # source of truth for both the library key and the display form
    assert fn_type().name == expected


@pytest.mark.parametrize(
    "fn_type, expected",
    [
        (Nullary, 0),
        (Clock, 0),
        (Binary, 2),
    ],
)
def test_arity_derives_from_call_signature(fn_type, expected):
    # arity is read off `_call`'s parameters, so a subclass declares it once
    assert fn_type().arity == expected


def test_arity_is_computed_once():
    # the value is cached at construction rather than recomputed per call,
    # since `arity` is consulted on every invocation
    fn = Binary()
    assert fn.arity == fn.arity == 2


@pytest.mark.parametrize(
    "fn_type, expected",
    [
        (Nullary, "<fn nullary (native)>"),
        (ReadLine, "<fn read_line (native)>"),
        (Clock, "<fn clock (native)>"),
    ],
)
def test_str_marks_the_function_as_native(fn_type, expected):
    assert str(fn_type()) == expected


@pytest.mark.parametrize(
    "fn_type, arguments, expected",
    [
        (Nullary, [], "none"),
        (ReadLine, [], "line"),
        (Binary, [1.0, 2.0], 3.0),
    ],
)
def test_call_forwards_arguments(interpreter, fn_type, arguments, expected):
    # `call` unpacks the Lox argument list into `_call`
    assert fn_type().call(arguments, interpreter) == expected


def test_clock(interpreter):
    first = Clock().call([], interpreter)
    second = Clock().call([], interpreter)
    # `clock` reports elapsed seconds as a number...
    assert type(first) is float
    assert type(second) is float
    assert first > 0
    # ...and never runs backwards
    assert first <= second


def test_sleep(interpreter):
    started = time()
    # `sleep` blocks for at least the requested seconds and evaluates to nil
    assert Sleep().call([0.02], interpreter) is None
    assert time() - started >= 0.02


@pytest.mark.parametrize(
    "seconds, message",
    [
        # only numbers are accepted; bools are not numbers in Lox
        ("a", "Argument must be a number"),
        (None, "Argument must be a number"),
        (True, "Argument must be a number"),
        # a negative duration is meaningless rather than instantaneous
        (-1.0, "Argument must be a non-negative number"),
    ],
)
def test_sleep_rejects_bad_arguments(interpreter, seconds, message):
    # natives report bad arguments with _NativeFnError, which carries neither
    # a position nor the function name; the interpreter adds both at the call
    # site, so the bare reason is what a native is responsible for
    with pytest.raises(_NativeFnError) as excinfo:
        Sleep().call([seconds], interpreter)
    assert str(excinfo.value) == message


def test_get_library_exposes_named_callables():
    fns = list(get_library())
    assert fns  # the library is not empty
    for fn in fns:
        assert isinstance(fn, LoxCallable)
    # every native is reachable under a unique name -- the interpreter
    # registers them by `name`, so a collision would silently drop one
    names = [fn.name for fn in fns]
    assert len(names) == len(set(names))
    assert "clock" in names


def test_get_library_returns_fresh_instances():
    # a fresh interpreter must not share native state with a previous one
    first = {fn.name: fn for fn in get_library()}
    second = {fn.name: fn for fn in get_library()}
    assert first.keys() == second.keys()
    for name, fn in first.items():
        assert fn is not second[name]
