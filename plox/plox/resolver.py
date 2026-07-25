from collections.abc import Generator
from contextlib import contextmanager

from plox.ast import (
    Stmt,
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
    Conditional,
    Logical,
    Binary,
    Unary,
    Call,
    Literal,
    Variable,
    Grouping,
)
from plox.common import Token, ResolverError


class Resolver(
    Stmt.Visitor[None],
    Expr.Visitor[None],
):
    def __init__(self) -> None:
        self._scopes: list[dict[str, bool]] = []
        self._errors: list[ResolverError] = []

    def resolve(self, statements: list[Stmt]) -> None:
        self._scopes.clear()
        self._errors.clear()

        # global, hence no _scope call
        self._resolve_block(statements)

        if self._errors:
            raise ExceptionGroup("Resolver errors", self._errors)

    def visit_function(self, s: Function) -> None:
        self._declare(s.name)
        self._define(s.name)
        self._resolve_function(s)

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
        if s.value is not None:
            self._resolve(s.value)

    def visit_while(self, s: While) -> None:
        self._resolve(s.condition)
        self._resolve(s.body)

    def visit_loopjump(self, s: LoopJump) -> None:
        pass

    def visit_block(self, s: Block) -> None:
        with self._scope():
            self._resolve_block(s.statements)

    def visit_expression(self, s: Expression) -> None:
        self._resolve(s.expression)

    def visit_assign(self, e: Assign) -> None:
        self._resolve(e.value)
        self._resolve_local(e, e.name.lexeme)

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
        self._resolve(e.callee)
        for arg in e.arguments:
            self._resolve(arg)

    def visit_literal(self, e: Literal) -> None:
        pass

    def visit_variable(self, e: Variable) -> None:
        name = e.name.lexeme
        if self._scopes and self._scopes[-1].get(name) is False:
            self._error("Can't read local variable in its own initializer", e.name)

        self._resolve_local(e, name)

    def visit_grouping(self, e: Grouping) -> None:
        self._resolve(e.expression)

    def _resolve(self, node: Expr | Stmt) -> None:
        node.accept(self)

    def _resolve_local(self, e: Variable | Assign, name: str) -> None:
        for distance, scope in enumerate(reversed(self._scopes)):
            if name in scope:
                e.set_distance(distance)
                break
        else:
            # assume global scope
            e.set_distance(None)

    def _resolve_block(self, block: list[Stmt]) -> None:
        for statement in block:
            self._resolve(statement)

    def _resolve_function(self, s: Function) -> None:
        with self._scope():
            for param in s.parameters:
                # bind function parameters
                self._declare(param)
                self._define(param)

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
    def _scope(self, *, enabled: bool = True) -> Generator[None]:
        if not enabled:
            # when not enabled, this is a no op
            yield
            return

        try:
            self._scopes.append({})
            yield
        finally:
            self._scopes.pop()

    def _error(self, msg: str, token: Token) -> None:
        # all errors must be reported through this method
        error = ResolverError(msg, token)
        self._errors.append(error)
