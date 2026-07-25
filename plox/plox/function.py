from plox.ast import Function
from plox.common import LoxCallable, LoxValue
from plox.environment import Environment
from plox.interpreter import Interpreter, _CallableReturn


class LoxFunction(LoxCallable):
    def __init__(
        self,
        decl: Function,
        closure: Environment,
    ):
        self._decl = decl
        self._closure = closure

    def call(
        self,
        arguments: list[LoxValue],
        interpreter: Interpreter,
    ) -> LoxValue:
        call_env = Environment(parent=self._closure)
        for param, arg in zip(self._decl.parameters, arguments, strict=True):
            call_env.define(param.lexeme, arg)

        try:
            # calling "private" method here, as LoxFunction
            # is tightly coupled with Interpreter's logic
            interpreter._execute_block(self._decl.body, call_env)
        except _CallableReturn as ret:
            return ret.value

        return None  # no return from fn code

    @property
    def name(self) -> str:
        return self._decl.name.lexeme

    @property
    def arity(self) -> int:
        return len(self._decl.parameters)

    def __str__(self) -> str:
        return f"<fn {self.name}>"
