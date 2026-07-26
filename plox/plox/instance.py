from typing import TYPE_CHECKING

from plox.common import Token, LoxObject, LoxValue
from plox.errors import InterpreterError

if TYPE_CHECKING:
    from plox.class_ import LoxClass
    from plox.function import LoxFunction


class LoxInstance(LoxObject):
    def __init__(self, class_: "LoxClass"):
        self._class = class_
        self._fields: dict[str, LoxValue] = {}

        # bound methods are idempotent, so we can cache them
        self._bound_method_cache: dict[str, "LoxFunction"] = {}

    def get(self, token: Token) -> LoxValue:
        name = token.lexeme
        if name in self._fields:
            return self._fields[name]

        if method := self._get_bound_method(name):
            # method is a property but not a field
            return method

        raise InterpreterError(f"Undefined property '{name}'", token)

    def set(self, token: Token, value: LoxValue) -> None:
        self._fields[token.lexeme] = value

    def __str__(self) -> str:
        return f"<instance of {self._class}>"

    def _get_bound_method(self, name: str) -> "LoxFunction | None":
        if bound_method := self._bound_method_cache.get(name):
            return bound_method
        if method := self._class.find_method(name):
            bound_method = method.bind(self)
            self._bound_method_cache[name] = bound_method
            return bound_method
        return None
