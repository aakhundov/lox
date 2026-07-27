from collections.abc import Generator, Iterable
from contextlib import contextmanager
from enum import Enum, auto

from plox.ast import (
    Program,
    Stmt,
    Class,
    Function,
    Var,
    For,
    If,
    Print,
    Return,
    While,
    LoopJump,
    Block,
    Expression,
    Expr,
    Assign,
    Set,
    Conditional,
    Logical,
    Binary,
    Unary,
    Call,
    Get,
    Literal,
    Super,
    This,
    Variable,
    Grouping,
)
from plox.common import Token
from plox.errors import ResolverError


class LoopType(Enum):
    FOR = auto()
    WHILE = auto()


class FunctionType(Enum):
    FUNCTION = auto()
    METHOD = auto()
    INIT = auto()


class ClassType(Enum):
    CLASS = auto()
    SUBCLASS = auto()


class Resolver(
    Stmt.Visitor[None],
    Expr.Visitor[None],
):
    _MAX_ARITY = 255

    def __init__(self) -> None:
        self._scopes: list[dict[str, bool]] = []
        self._errors: list[ResolverError] = []
        self._in_loop: list[LoopType] = []
        self._in_function: list[FunctionType] = []
        self._in_class: list[ClassType] = []

    def resolve(self, program: Program) -> None:
        self._errors.clear()

        # global, hence no _scope call
        self._resolve_block(program.statements)

        assert not self._scopes
        assert not self._in_loop
        assert not self._in_function
        assert not self._in_class

        if self._errors:
            raise ExceptionGroup("Resolver errors", self._errors)

    def visit_class(self, s: Class) -> None:
        self._declare(s.name)
        self._define(s.name)

        if s.superclass is not None:
            if s.superclass.name.lexeme == s.name.lexeme:
                self._error(
                    "A class can't inherit from itself",
                    s.superclass.name,
                )

            self._resolve(s.superclass)

        with self._class(
            ClassType.SUBCLASS if s.superclass is not None else ClassType.CLASS
        ):
            with self._scope(enabled=(s.superclass is not None)) as super_scope:
                if super_scope is not None:
                    super_scope["super"] = True

                with self._scope() as this_scope:
                    assert this_scope is not None
                    this_scope["this"] = True

                    for method in s.methods:
                        self._resolve_function(
                            method,
                            FunctionType.INIT
                            if method.name.lexeme == "init"
                            else FunctionType.METHOD,
                        )

    def visit_function(self, s: Function) -> None:
        if len(s.parameters) > self._MAX_ARITY:
            self._error(f"Max {self._MAX_ARITY} parameters allowed", s.name)

        self._declare(s.name)
        self._define(s.name)
        self._resolve_function(s, FunctionType.FUNCTION)

    def visit_var(self, s: Var) -> None:
        self._declare(s.name)

        if s.initializer is not None:
            self._resolve(s.initializer)

        self._define(s.name)

    def visit_for(self, s: For) -> None:
        with self._scope(enabled=isinstance(s.initializer, Var)):
            if s.initializer is not None:
                self._resolve(s.initializer)
            if s.condition is not None:
                self._resolve(s.condition)
            if s.increment is not None:
                self._resolve(s.increment)

            with self._loop(LoopType.FOR):
                self._resolve(s.body)

    def visit_if(self, s: If) -> None:
        self._resolve(s.condition)
        self._resolve(s.then_branch)
        if s.else_branch is not None:
            self._resolve(s.else_branch)

    def visit_print(self, s: Print) -> None:
        for e in s.expressions:
            self._resolve(e)

    def visit_return(self, s: Return) -> None:
        if not self._in_function:
            self._error(
                "return allowed only inside function body",
                s.keyword,
            )

        if s.value is not None:
            if self._in_function and self._in_function[-1] == FunctionType.INIT:
                self._error(
                    "Can't return value from initializer",
                    s.keyword,
                )

            self._resolve(s.value)

    def visit_while(self, s: While) -> None:
        self._resolve(s.condition)

        with self._loop(LoopType.WHILE):
            self._resolve(s.body)

    def visit_loopjump(self, s: LoopJump) -> None:
        if not self._in_loop:
            self._error(
                f"{s.keyword.lexeme} allowed only inside loop body",
                s.keyword,
            )

    def visit_block(self, s: Block) -> None:
        with self._scope():
            self._resolve_block(s.statements)

    def visit_expression(self, s: Expression) -> None:
        self._resolve(s.expression)

    def visit_assign(self, e: Assign) -> None:
        self._resolve(e.value)
        self._resolve_local(e, e.name.lexeme)

    def visit_set(self, e: Set) -> None:
        self._resolve(e.object)
        self._resolve(e.value)

    def visit_conditional(self, e: Conditional) -> None:
        self._resolve(e.condition)
        self._resolve(e.then_expression)
        self._resolve(e.else_expression)

    def visit_logical(self, e: Logical) -> None:
        self._resolve(e.left)
        self._resolve(e.right)

    def visit_binary(self, e: Binary) -> None:
        self._resolve(e.left)
        self._resolve(e.right)

    def visit_unary(self, e: Unary) -> None:
        self._resolve(e.right)

    def visit_call(self, e: Call) -> None:
        if len(e.arguments) > self._MAX_ARITY:
            self._error(f"Max {self._MAX_ARITY} arguments allowed", e.paren)

        self._resolve(e.callee)
        for arg in e.arguments:
            self._resolve(arg)

    def visit_get(self, e: Get) -> None:
        self._resolve(e.object)

    def visit_literal(self, e: Literal) -> None:
        pass

    def visit_super(self, e: Super) -> None:
        if not self._in_class:
            self._error("Can't use super outside of class", e.keyword)
        elif self._in_class[-1] != ClassType.SUBCLASS:
            self._error("Can't use super in class with no superclass", e.keyword)

        self._resolve_local(e, e.keyword.lexeme)

    def visit_this(self, e: This) -> None:
        if not self._in_class:
            self._error("Can't use 'this' outside of class", e.keyword)
        else:
            self._resolve_local(e, e.keyword.lexeme)

    def visit_variable(self, e: Variable) -> None:
        name = e.name.lexeme
        if self._scopes and self._scopes[-1].get(name) is False:
            self._error("Can't read local variable in its own initializer", e.name)
        else:
            self._resolve_local(e, name)

    def visit_grouping(self, e: Grouping) -> None:
        self._resolve(e.expression)

    def _resolve(self, node: Expr | Stmt) -> None:
        node.accept(self)

    # this accepts AST nodes with the distance defined
    def _resolve_local(self, e: Variable | Assign | This | Super, name: str) -> None:
        for distance, scope in enumerate(reversed(self._scopes)):
            if name in scope:
                e.set_distance(distance)
                break
        else:
            # assume global scope
            e.set_distance(None)

    def _resolve_block(self, block: Iterable[Stmt]) -> None:
        for statement in block:
            self._resolve(statement)

    def _resolve_function(self, s: Function, type_: FunctionType) -> None:
        with self._scope():
            for param in s.parameters:
                # bind function parameters
                self._declare(param)
                self._define(param)

            with self._function(type_):
                self._resolve_block(s.body)

    def _declare(self, name: Token) -> None:
        if not self._scopes:
            return  # global scope

        if name.lexeme in self._scopes[-1]:
            self._error("Already a variable with this name in this scope", name)

        # declared but not defined yet
        self._scopes[-1][name.lexeme] = False

    def _define(self, name: Token) -> None:
        if not self._scopes:
            return  # global scope

        # now declared and defined
        self._scopes[-1][name.lexeme] = True

    @contextmanager
    def _scope(self, *, enabled: bool = True) -> Generator[dict[str, bool] | None]:
        if not enabled:
            # when not enabled, this is a no op
            yield None
            return

        try:
            self._scopes.append({})
            yield self._scopes[-1]
        finally:
            self._scopes.pop()

    @contextmanager
    def _loop(self, type_: LoopType) -> Generator[None]:
        try:
            self._in_loop.append(type_)
            yield
        finally:
            self._in_loop.pop()

    @contextmanager
    def _function(self, type_: FunctionType) -> Generator[None]:
        prev_in_loop = self._in_loop
        try:
            self._in_loop = []  # reset loop within function
            self._in_function.append(type_)
            yield
        finally:
            self._in_function.pop()
            self._in_loop = prev_in_loop  # restore loop

    @contextmanager
    def _class(self, type_: ClassType) -> Generator[None]:
        try:
            self._in_class.append(type_)
            yield
        finally:
            self._in_class.pop()

    def _error(self, msg: str, token: Token) -> None:
        # all errors must be reported through this method
        error = ResolverError(msg, token)
        self._errors.append(error)
