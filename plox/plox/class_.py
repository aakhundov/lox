from typing import TYPE_CHECKING

from plox.common import LoxCallable, LoxObject, LoxValue
from plox.instance import LoxInstance

if TYPE_CHECKING:
    from plox.function import LoxFunction
    from plox.interpreter import Interpreter


class LoxClass(LoxObject, LoxCallable):
    def __init__(self, name: str, methods: dict[str, "LoxFunction"]) -> None:
        self._name = name
        self._methods = methods

    def call(self, arguments: list[LoxValue], interpreter: "Interpreter") -> LoxValue:
        instance = LoxInstance(self)

        if init := self.find_method("init"):
            init.bind(instance).call(arguments, interpreter)

        return instance

    def find_method(self, name: str) -> "LoxFunction | None":
        return self._methods.get(name)

    @property
    def name(self) -> str:
        return self._name

    @property
    def arity(self) -> int:
        if init := self.find_method("init"):
            return init.arity

        return 0

    def __str__(self) -> str:
        return f"<class {self._name}>"
