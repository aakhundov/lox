from typing import TYPE_CHECKING

from plox.common import LoxCallable, LoxObject, LoxValue
from plox.environment import Environment
from plox.errors import CallableReturn

if TYPE_CHECKING:
    from plox.ast import Function
    from plox.interpreter import Interpreter
    from plox.instance import LoxInstance


class LoxFunction(LoxObject, LoxCallable):
    def __init__(
        self,
        decl: "Function",
        closure: Environment,
        *,
        is_init: bool = False,
    ):
        self._decl = decl
        self._closure = closure
        self._is_init = is_init

    def call(
        self,
        arguments: list[LoxValue],
        interpreter: "Interpreter",
    ) -> LoxValue:
        call_env = Environment(parent=self._closure)
        for param, arg in zip(self._decl.parameters, arguments, strict=True):
            call_env.define(param.lexeme, arg)

        try:
            # calling "private" method here, as LoxFunction
            # is tightly coupled with Interpreter's logic
            interpreter._execute_block(self._decl.body, call_env)
        except CallableReturn as ret:
            if self._is_init:
                # init always returns `this`
                return self._closure.get_at(0, "this")

            return ret.value

        if self._is_init:
            # init always returns `this`
            return self._closure.get_at(0, "this")

        return None  # no return from fn code

    def bind(self, instance: "LoxInstance") -> "LoxFunction":
        # child env with `this` bound to the instance
        env_with_this = Environment(parent=self._closure)
        env_with_this.define("this", instance)

        return LoxFunction(
            decl=self._decl,
            closure=env_with_this,
            is_init=self._is_init,
        )

    @property
    def name(self) -> str:
        return self._decl.name.lexeme

    @property
    def arity(self) -> int:
        return len(self._decl.parameters)

    def __str__(self) -> str:
        return f"<fn {self.name}>"
