import operator
from collections.abc import Callable
from typing import NoReturn

from plox.ast.expr import Expr, Grouping, Binary, Unary, Literal
from plox.common import (
    LoxErrorFromToken,
    Token,
    TokenType as TT,
    LoxValue,
    is_equal,
    is_truthy,
)


class InterpreterError(LoxErrorFromToken):
    pass


class Interpreter(Expr.Visitor[LoxValue]):
    # TT.PLUS isn't here because it applies to both float and str
    _FLOAT_BINARY_OPS: dict[TT, Callable[[float, float], float | bool]] = {
        TT.MINUS: operator.sub,
        TT.STAR: operator.mul,
        TT.SLASH: operator.truediv,
        TT.GREATER: operator.gt,
        TT.GREATER_EQUAL: operator.ge,
        TT.LESS: operator.lt,
        TT.LESS_EQUAL: operator.le,
    }

    def interpret(self, e: Expr) -> LoxValue:
        return self._evaluate(e)

    def visit_grouping(self, e: Grouping) -> LoxValue:
        return self._evaluate(e.expression)

    def visit_binary(self, e: Binary) -> LoxValue:
        op = e.operator
        left = self._evaluate(e.left)
        right = self._evaluate(e.right)

        if op.type == TT.PLUS:
            if isinstance(left, float) and isinstance(right, float):
                return left + right  # float addition
            if isinstance(left, str) and isinstance(right, str):
                return left + right  # str concatenation
            self._raise("Operands must both be number or string", op)

        if op.type in self._FLOAT_BINARY_OPS:
            if not isinstance(left, float):
                self._raise("Left operand must be a number", op)
            if not isinstance(right, float):
                self._raise("Right operand must be a number", op)
            if op.type == TT.SLASH and right == 0:
                self._raise("Division by zero", op)
            return self._FLOAT_BINARY_OPS[op.type](left, right)

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

    def _evaluate(self, e: Expr) -> LoxValue:
        return e.accept(self)

    def _raise(self, msg: str, token: Token) -> NoReturn:
        raise InterpreterError(msg, token)
