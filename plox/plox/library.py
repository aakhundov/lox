import re
from abc import ABC, abstractmethod
from collections.abc import Iterable
from inspect import signature
from time import sleep, time
from typing import Any, TYPE_CHECKING

from plox.common import LoxCallable, LoxObject, LoxValue
from plox.errors import NativeFnError

if TYPE_CHECKING:
    from plox.interpreter import Interpreter


_CAMEL_PATTERN = re.compile(r"(?<!^)(?=[A-Z])")


class LoxNativeFn(LoxObject, LoxCallable, ABC):
    def __init__(self) -> None:
        self._name = self._get_name()
        self._arity = len(signature(self._call).parameters)

    def call(
        self,
        arguments: list[LoxValue],
        interpreter: "Interpreter",
    ) -> LoxValue:
        return self._call(*arguments)

    @property
    def name(self) -> str:
        return self._name

    @property
    def arity(self) -> int:
        return self._arity

    def __str__(self) -> str:
        return f"<fn {self.name} (native)>"

    def _get_name(self) -> str:
        cls_name = type(self).__name__  # camel case
        return _CAMEL_PATTERN.sub("_", cls_name).lower()  # snake case

    @abstractmethod
    def _call(self, *args: Any, **kwargs: Any) -> LoxValue: ...


class Clock(LoxNativeFn):
    def _call(self) -> LoxValue:
        return time()


class Sleep(LoxNativeFn):
    def _call(self, seconds: LoxValue) -> LoxValue:
        # bools are not floats in Python, so this rejects them too
        if not isinstance(seconds, float):
            raise NativeFnError("Argument must be a number")
        if seconds < 0:
            raise NativeFnError("Argument must be a non-negative number")

        sleep(seconds)
        return None


_LIBRARY_FN_TYPES: tuple[type[LoxNativeFn], ...] = (
    Clock,
    Sleep,
)


def get_library() -> Iterable[LoxNativeFn]:
    return (t() for t in _LIBRARY_FN_TYPES)
