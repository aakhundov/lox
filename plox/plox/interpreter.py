import operator
from collections.abc import Callable
from typing import NoReturn

from plox.ast import (
    Expr,
    Grouping,
    Binary,
    Unary,
    Literal,
    Variable,
    Assign,
    Stmt,
    Print,
    Expression,
    Var,
    Block,
)
from plox.common import (
    Token,
    TokenType as TT,
    InterpreterError,
    LoxValue,
    is_equal,
    is_truthy,
    to_str,
)
from plox.environment import Environment


class Interpreter(
    Expr.Visitor[LoxValue],
    Stmt.Visitor[None],
):
    _FLOAT_BINARY_OPS: dict[
        TT,
        Callable[
            [float, float],
            float,
        ],
    ] = {
        TT.MINUS: operator.sub,
        TT.STAR: operator.mul,
        TT.SLASH: operator.truediv,
    }

    _FLOAT_OR_STR_BINARY_OPS: dict[
        TT,
        Callable[
            [float | str, float | str],
            float | bool | str,
        ],
    ] = {
        TT.PLUS: operator.add,
        TT.GREATER: operator.gt,
        TT.GREATER_EQUAL: operator.ge,
        TT.LESS: operator.lt,
        TT.LESS_EQUAL: operator.le,
    }

    def __init__(
        self,
        *,
        environment: Environment | None = None,
        print_fn: Callable[[list[LoxValue]], None] | None = None,
    ) -> None:
        if environment is None:
            environment = Environment()
        if print_fn is None:
            print_fn = self._default_print_fn

        self._env = environment
        self._print_fn = print_fn

    def interpret(self, statements: list[Stmt]) -> None:
        for statement in statements:
            self._execute(statement)

    def visit_print(self, s: Print) -> None:
        values = [self._evaluate(e) for e in s.expressions]
        self._print_fn(values)

    def visit_expression(self, s: Expression) -> None:
        self._evaluate(s.expression)

    def visit_var(self, s: Var) -> None:
        value = None
        if s.initializer is not None:
            value = self._evaluate(s.initializer)

        self._env.define(s.name.lexeme, value)

    def visit_block(self, s: Block) -> None:
        child_env = Environment(parent=self._env)
        self._execute_block(s.statements, child_env)

    def visit_grouping(self, e: Grouping) -> LoxValue:
        return self._evaluate(e.expression)

    def visit_binary(self, e: Binary) -> LoxValue:
        op = e.operator
        left = self._evaluate(e.left)
        right = self._evaluate(e.right)

        # can only take float args
        if float_fn := self._FLOAT_BINARY_OPS.get(op.type):
            if not isinstance(left, float):
                self._raise("Left operand must be a number", op)
            if not isinstance(right, float):
                self._raise("Right operand must be a number", op)
            if op.type == TT.SLASH and right == 0:
                self._raise("Division by zero", op)
            return float_fn(left, right)

        # can take float or str args
        if float_or_str_fn := self._FLOAT_OR_STR_BINARY_OPS.get(op.type):
            if isinstance(left, float) and isinstance(right, float):
                return float_or_str_fn(left, right)  # float operation
            if isinstance(left, str) and isinstance(right, str):
                return float_or_str_fn(left, right)  # str operation
            self._raise("Operands must both be number or string", op)

        # can take any arg types
        if op.type == TT.EQUAL_EQUAL:
            return is_equal(left, right)
        if op.type == TT.BANG_EQUAL:
            return not is_equal(left, right)

        # this line must be unreachable
        self._raise("Unknown binary op", op)

    def visit_unary(self, e: Unary) -> LoxValue:
        op = e.operator
        operand = self._evaluate(e.right)

        if op.type == TT.BANG:
            return not is_truthy(operand)
        if op.type == TT.MINUS:
            if not isinstance(operand, float):
                self._raise("Operand must be a number", op)
            return -operand

        # this line must be unreachable
        self._raise("Unknown unary op", op)

    def visit_literal(self, e: Literal) -> LoxValue:
        return e.value

    def visit_variable(self, e: Variable) -> LoxValue:
        return self._env.get(e.name)

    def visit_assign(self, e: Assign) -> LoxValue:
        value = self._evaluate(e.value)
        self._env.assign(e.name, value)
        return value

    def _execute_block(
        self,
        statements: list[Stmt],
        block_env: Environment,
    ) -> None:
        previous_env = self._env
        try:
            self._env = block_env
            for statement in statements:
                self._execute(statement)
        finally:
            # restore the previous env
            self._env = previous_env

    def _execute(self, s: Stmt) -> None:
        return s.accept(self)

    def _evaluate(self, e: Expr) -> LoxValue:
        return e.accept(self)

    def _raise(self, msg: str, token: Token) -> NoReturn:
        raise InterpreterError(msg, token)

    @staticmethod
    def _default_print_fn(values: list[LoxValue]) -> None:
        strs = [to_str(val) for val in values]
        print(" ".join(strs))
