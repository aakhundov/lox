"""Fixtures shared by more than one suite.

Only things that belong to a contract *every* layer takes part in live here --
how a phase reports its errors, where an error points, and the scan/parse/resolve
pipeline the later phases are driven through. Vocabulary specific to one layer
(rendering an AST, collecting printed values, listing resolved distances) stays
in the suite that owns it.
"""

import pytest

from plox.parser import Parser
from plox.resolver import Resolver
from plox.scanner import Scanner


@pytest.fixture
def collect_errors():
    """Return a helper running `fn(*args)` expecting failure, returning its errors.

    Every phase accumulates the errors it finds and reports them together by
    raising a single ExceptionGroup at the end. This unwraps that group into a
    flat list of `error_type`, in source order; the isinstance check doubles as
    an assertion that the group is flat, with no nested groups.
    """

    def _collect_errors(error_type, fn, *args):
        with pytest.raises(ExceptionGroup) as excinfo:
            fn(*args)

        errors = []
        for error in excinfo.value.exceptions:
            assert isinstance(error, error_type)  # flat: no nested groups
            errors.append(error)
        return errors

    return _collect_errors


@pytest.fixture
def error_position():
    """Return a helper giving the single source position an error points at.

    `get_line_info` yields one (line, col) pair per reported position. Only the
    interpreter reports more than one -- a runtime error collects a position per
    call it unwound through -- so for every other phase the unpack doubles as an
    assertion that there is exactly one.
    """

    def _error_position(error):
        (position,) = error.get_line_info()
        return position

    return _error_position


@pytest.fixture
def error_stack():
    """Return a helper listing every source position an error points at.

    The positions come back outermost call first, with the one that actually
    failed last.
    """

    def _error_stack(error):
        return list(error.get_line_info())

    return _error_stack


@pytest.fixture
def parse_program():
    """Return a helper that scans then parses `source` into a Program.

    Driving the parser through the real Scanner mirrors how it is used in
    practice and keeps expectations free of token-construction details.
    """

    def _parse_program(source):
        return Parser(Scanner(source).scan()).parse()

    return _parse_program


@pytest.fixture
def resolve_program(parse_program):
    """Return a helper that scans, parses and resolves `source` into a Program.

    The resolver writes scope distances onto the AST nodes it visits, so the
    Program handed back is the same one, resolved in place.
    """

    def _resolve_program(source):
        program = parse_program(source)
        Resolver().resolve(program)
        return program

    return _resolve_program
