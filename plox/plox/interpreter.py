import operator
from collections.abc import Callable, Generator
from contextlib import contextmanager
from typing import NoReturn

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
from plox.common import (
    Token,
    TokenType as TT,
    InterpreterError,
    LoxCallable,
    LoxValue,
    is_equal,
    is_truthy,
    to_str,
)
from plox.environment import Environment


class _LoopBreak(Exception):
    pass


class _LoopContinue(Exception):
    pass


class _CallableReturn(Exception):
    def __init__(self, value: LoxValue):
        self._value = value

    @property
    def value(self) -> LoxValue:
        return self._value


class _NativeFnError(Exception):
    pass


class Interpreter(
    Stmt.Visitor[None],
    Expr.Visitor[LoxValue],
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
            ...,  # too flexible arg types
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
        self._globals = environment
        self._print_fn = print_fn

        self._register_library()

    def interpret(self, statements: list[Stmt]) -> None:
        for statement in statements:
            self._execute(statement)

    def visit_function(self, s: Function) -> None:
        # local to avoid circular import
        from plox.function import LoxFunction

        fn = LoxFunction(s, self._env)
        self._env.define(fn.name, fn)

    def visit_var(self, s: Var) -> None:
        value = None
        if s.initializer is not None:
            value = self._evaluate(s.initializer)

        self._env.define(s.name.lexeme, value)

    def visit_for(self, s: For) -> None:
        def _cond() -> bool:
            if s.condition is not None:
                return is_truthy(self._evaluate(s.condition))
            return True

        # execute the loop in a sub-env if init is a variable decl
        with self._nested_env(enabled=isinstance(s.initializer, Var)):
            if s.initializer is not None:
                self._execute(s.initializer)

            while _cond():
                try:
                    self._execute(s.body)
                except _LoopContinue:
                    # still need to increment
                    pass
                except _LoopBreak:
                    break

                if s.increment is not None:
                    self._evaluate(s.increment)

    def visit_if(self, s: If) -> None:
        if is_truthy(self._evaluate(s.condition)):
            self._execute(s.then_branch)
        elif s.else_branch is not None:
            self._execute(s.else_branch)

    def visit_print(self, s: Print) -> None:
        values = [self._evaluate(e) for e in s.expressions]
        self._print_fn(values)

    def visit_return(self, s: Return) -> None:
        value = None
        if s.value is not None:
            value = self._evaluate(s.value)

        raise _CallableReturn(value)

    def visit_while(self, s: While) -> None:
        while is_truthy(self._evaluate(s.condition)):
            try:
                self._execute(s.body)
            except _LoopContinue:
                continue
            except _LoopBreak:
                break

    def visit_loopjump(self, s: LoopJump) -> None:
        if s.keyword.type == TT.CONTINUE:
            raise _LoopContinue()
        if s.keyword.type == TT.BREAK:
            raise _LoopBreak()

        # this line must be unreachable
        self._raise("Unknown loop jump statement", s.keyword)

    def visit_block(self, s: Block) -> None:
        self._execute_block(s.statements)

    def visit_expression(self, s: Expression) -> None:
        self._evaluate(s.expression)

    def visit_assign(self, e: Assign) -> LoxValue:
        value = self._evaluate(e.value)
        self._env.assign(e.name, value)
        return value

    def visit_conditional(self, e: Conditional) -> LoxValue:
        if is_truthy(self._evaluate(e.condition)):
            return self._evaluate(e.then_expression)
        else:
            return self._evaluate(e.else_expression)

    def visit_logical(self, e: Logical) -> LoxValue:
        op = e.operator
        left = self._evaluate(e.left)

        if op.type == TT.AND:
            if not is_truthy(left):
                return left  # short-circuit
            else:
                return self._evaluate(e.right)
        if op.type == TT.OR:
            if is_truthy(left):
                return left  # short-circuit
            else:
                return self._evaluate(e.right)

        # this line must be unreachable
        self._raise("Unknown logical op", op)

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

    def visit_call(self, e: Call) -> LoxValue:
        callee = self._evaluate(e.callee)
        arguments = [self._evaluate(arg) for arg in e.arguments]

        if not isinstance(callee, LoxCallable):
            self._raise("Can only call functions and methods", e.paren)

        if callee.arity != len(arguments):
            suffix = "s" if callee.arity != 1 else ""
            self._raise(
                f"Expected {callee.arity} argument{suffix} but got {len(arguments)}",
                e.paren,
            )

        try:
            return callee.call(arguments, self)
        except _NativeFnError as error:
            # calling a native function raised an error
            self._raise(f"Error calling '{callee.name}': {error}", e.paren)

    def visit_literal(self, e: Literal) -> LoxValue:
        return e.value

    def visit_variable(self, e: Variable) -> LoxValue:
        return self._env.get(e.name)

    def visit_grouping(self, e: Grouping) -> LoxValue:
        return self._evaluate(e.expression)

    def _execute_block(
        self,
        statements: list[Stmt],
        block_env: Environment | None = None,
    ) -> None:
        with self._nested_env(block_env):
            for statement in statements:
                self._execute(statement)

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

    @contextmanager
    def _nested_env(
        self,
        new_env: Environment | None = None,
        *,
        enabled: bool = True,
    ) -> Generator[Environment]:
        if not enabled:
            # when not enabled, this is a no op
            yield self._env
            return

        # keep the previous env
        previous_env = self._env
        if new_env is None:
            # make a child env of the previous env
            new_env = Environment(parent=previous_env)
        try:
            self._env = new_env
            yield new_env
        finally:
            # restore the previous env
            self._env = previous_env

    def _register_library(self) -> None:
        from plox.library import get_library

        # register library
        for fn in get_library():
            self._globals.define(fn.name, fn)
