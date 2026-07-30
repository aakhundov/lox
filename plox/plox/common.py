import math
from abc import ABC, abstractmethod
from dataclasses import dataclass
from decimal import Decimal
from enum import Enum, auto
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from plox.interpreter import Interpreter


@dataclass(frozen=True)
class Location:
    name: str
    line: str
    line_num: int
    col_num: int


@dataclass(frozen=True, eq=False)
class Source:
    name: str
    code: str
    lines: tuple[str, ...]

    @classmethod
    def create(cls, code: str, name: str | None = None) -> "Source":
        if name is None:
            name = "<anon>"
        lines = tuple(code.split("\n"))
        return cls(name, code, lines)

    def get_location(self, line_num: int, col_num: int) -> Location:
        line = self.lines[line_num - 1]
        return Location(self.name, line, line_num, col_num)


class TokenType(Enum):
    # single-char
    LEFT_PAREN = auto()
    RIGHT_PAREN = auto()
    LEFT_BRACE = auto()
    RIGHT_BRACE = auto()
    COMMA = auto()
    DOT = auto()
    MINUS = auto()
    PLUS = auto()
    SEMICOLON = auto()
    SLASH = auto()
    STAR = auto()

    # one- or two-char
    BANG = auto()
    BANG_EQUAL = auto()
    EQUAL = auto()
    EQUAL_EQUAL = auto()
    GREATER = auto()
    GREATER_EQUAL = auto()
    LESS = auto()
    LESS_EQUAL = auto()

    # ternary
    QUESTION = auto()
    COLON = auto()

    # literals
    IDENTIFIER = auto()
    STRING = auto()
    NUMBER = auto()

    # keywords
    AND = auto()
    CLASS = auto()
    ELSE = auto()
    FALSE = auto()
    FUN = auto()
    FOR = auto()
    IF = auto()
    NIL = auto()
    OR = auto()
    PRINT = auto()
    RETURN = auto()
    SUPER = auto()
    THIS = auto()
    TRUE = auto()
    VAR = auto()
    WHILE = auto()
    BREAK = auto()
    CONTINUE = auto()

    EOF = auto()


@dataclass
class Token:
    type: TokenType
    lexeme: str
    literal: float | str | None

    # source code
    source: Source

    # source position
    offset: int | None = None
    line_num: int | None = None
    col_num: int | None = None

    def get_location(self) -> Location:
        assert self.line_num is not None
        assert self.col_num is not None
        return self.source.get_location(self.line_num, self.col_num)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Token):
            return NotImplemented

        # ignore source position
        return (
            self.type == other.type
            and self.lexeme == other.lexeme
            and self.literal == other.literal
        )

    def __hash__(self) -> int:
        return hash((self.type, self.lexeme, self.literal))

    def __str__(self) -> str:
        desc = ""
        if self.literal is not None:
            desc = f"[{self.literal}] "
        elif self.type == TokenType.IDENTIFIER:
            desc = f"[{self.lexeme}] "

        return f"{self.type.name} {desc}({self.line_num}:{self.col_num})"


class LoxObject(ABC):
    @abstractmethod
    def __str__(self) -> str: ...


LoxValue = bool | float | str | LoxObject | None


class LoxCallable(ABC):
    @abstractmethod
    def call(
        self,
        arguments: list[LoxValue],
        interpreter: "Interpreter",
    ) -> LoxValue: ...

    @property
    @abstractmethod
    def name(self) -> str: ...

    @property
    @abstractmethod
    def arity(self) -> int: ...

    @abstractmethod
    def __str__(self) -> str: ...


def is_equal(a: LoxValue, b: LoxValue) -> bool:
    if type(a) is not type(b):
        # must be same type to be equal
        return False
    return a == b


def is_truthy(val: LoxValue) -> bool:
    if val is None:
        return False
    if isinstance(val, float) and val == 0:
        # zero numbers are falsy
        return False
    if isinstance(val, str) and val == "":
        # empty strings are falsy
        return False
    if isinstance(val, bool):
        return val
    return True


def to_str(val: LoxValue) -> str:
    if isinstance(val, bool):
        return str(val).lower()
    if isinstance(val, float) and val.is_integer():
        return str(int(val))
    if val is None:
        return "nil"
    return str(val)


def to_repr(val: LoxValue) -> str:
    if isinstance(val, str):
        return f'"{val}"'
    if isinstance(val, float) and math.isfinite(val) and not val.is_integer():
        return format(Decimal(repr(val)), "f")
    return to_str(val)
