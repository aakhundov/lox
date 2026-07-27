"""A tree-walking interpreter for the Lox language, in Python.

The package is for running an interpreter of your own: you feed it source and
decide what to do with what comes out. `__all__` is what it promises for that --
the four phases a source string runs through, the data those phases hand each
other, the errors they report, and the seam for adding a native function. Every
name in it appears in the signature of something else in it, so the set is
closed: a caller never has to reach for a type the package does not export.

Everything else is an implementation detail, importable by module path but not
part of the surface: the AST node types and both `Visitor` protocols (the
`visit_*` methods are dispatch targets for the generated `accept`, not calls a
consumer makes), and the runtime values `LoxClass`, `LoxInstance` and
`LoxFunction`, which are met through `LoxValue` rather than named directly.

The REPL is not part of it either. It is how the package is run as a console
app rather than something a caller drives, so it stays behind an on-demand
`from plox.repl import main` -- which also keeps its `prompt_toolkit` dependency
out of the cost of `import plox`. The built-in natives are likewise internal:
`Interpreter` loads them itself, and a caller adds their own by subclassing
`LoxNativeFn` and defining the instance in an `Environment`.
"""

from plox.ast import Program
from plox.common import (
    Token,
    TokenType,
    LoxCallable,
    LoxObject,
    LoxValue,
    to_str,
)
from plox.environment import Environment
from plox.errors import (
    LoxError,
    ScannerError,
    ParserError,
    ResolverError,
    InterpreterError,
    NativeFnError,
)
from plox.interpreter import Interpreter
from plox.library import LoxNativeFn
from plox.parser import Parser
from plox.resolver import Resolver
from plox.scanner import Scanner

__all__ = [
    # the pipeline, in the order a source string runs through it
    "Scanner",
    "Parser",
    "Resolver",
    "Interpreter",
    # the data the phases hand each other: tokens out of the scanner, a
    # resolved Program into the interpreter, LoxValues out of `print_fn`
    "Token",
    "TokenType",
    "Program",
    "LoxValue",
    "LoxObject",
    "to_str",
    # the globals an Interpreter is built around and hands back
    "Environment",
    # the errors each phase reports, and their common base to catch
    "LoxError",
    "ScannerError",
    "ParserError",
    "ResolverError",
    "InterpreterError",
    # the seam for adding a native function of your own
    "LoxCallable",
    "LoxNativeFn",
    "NativeFnError",
]
