"""Tests for the helpers in plox.common.

`to_str` and `to_repr` are two different jobs and the split is deliberate:
`to_str` renders a value for a *reader* (it backs `print` and error messages),
`to_repr` renders one as *Lox source* -- text the Scanner reads back as the
same literal. The printers and the REPL's `:env` dump use `to_repr`; only its
source-fidelity obligations are tested here.
"""

import math

import pytest

from plox.common import to_repr
from plox.scanner import Scanner


@pytest.fixture
def scan_literal():
    """Return a helper giving the single literal `source` scans to.

    The unpack is the assertion: text that is one Lox literal produces exactly
    one token before EOF. Anything that fragments -- an exponent, an unquoted
    string -- fails here rather than somewhere downstream.
    """

    def _scan_literal(source):
        token, eof = Scanner(source).scan()
        assert eof.type.name == "EOF"
        return token.literal

    return _scan_literal


@pytest.mark.parametrize(
    "value, expected",
    [
        (123.0, "123"),
        (45.67, "45.67"),
        (0.5, "0.5"),
        ("hello", '"hello"'),
        ("", '""'),
        (True, "true"),
        (False, "false"),
        (None, "nil"),
    ],
)
def test_to_repr(value, expected):
    assert to_repr(value) == expected


@pytest.mark.parametrize(
    "value, expected",
    [
        (0.0000001, "0.0000001"),
        (1.25e-5, "0.0000125"),
        (1e-4, "0.0001"),
        (1e16, "10000000000000000"),
        (-0.0000001, "-0.0000001"),
    ],
)
def test_to_repr_number_never_uses_exponent_notation(value, expected):
    # Lox numbers are digits with an optional fractional part -- there is no
    # exponent syntax -- so Python's `1e-07` would not scan back as a number
    assert to_repr(value) == expected
    assert "e" not in to_repr(value)


@pytest.mark.parametrize(
    "value",
    [0.0, 1.0, 123.0, 45.67, 0.5, 0.0000001, 1.25e-5, 1e16, "hello", ""],
)
def test_to_repr_output_scans_back_to_the_same_value(scan_literal, value):
    # the contract in one line, for the values the scanner carries as literals.
    # Restricted to what a Literal node can hold: Lox has no negative literals
    # (`-1` is a unary operator applied to `1`), so a negative number is two
    # tokens by construction, not one
    assert scan_literal(to_repr(value)) == value


@pytest.mark.parametrize(
    "value, lexeme", [(True, "true"), (False, "false"), (None, "nil")]
)
def test_to_repr_output_scans_back_to_the_matching_keyword(value, lexeme):
    # `true`/`false`/`nil` are keywords, not literal-bearing tokens -- the
    # scanner leaves their `literal` as None and the value lives in the token
    # type -- so their round trip is checked through the lexeme
    token, eof = Scanner(to_repr(value)).scan()
    assert eof.type.name == "EOF"
    assert token.lexeme == lexeme


def test_to_repr_string_is_quoted():
    assert to_repr("hi") == '"hi"'
    # no escaping is needed or wanted: the scanner cannot produce a string
    # containing a quote, and a backslash is a literal backslash in Lox
    assert to_repr("a\\b") == '"a\\b"'
    # a string may span lines, and the line breaks belong to the literal
    assert to_repr("a\nb") == '"a\nb"'


@pytest.mark.parametrize("value", [math.inf, -math.inf, math.nan])
def test_to_repr_leaves_non_finite_numbers_to_to_str(value):
    # no literal can be non-finite -- these only arise at runtime, where the
    # `:env` dump may meet one. There is no Lox source for them, so `to_repr`
    # renders them the way `to_str` does rather than inventing syntax; what it
    # must not do is emit Decimal's `Infinity`/`NaN`, which scan as identifiers
    assert to_repr(value) == str(value)
    assert "Infinity" not in to_repr(value)
    assert "NaN" not in to_repr(value)
