from dataclasses import dataclass
from enum import Enum, auto


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

    EOF = auto()


@dataclass
class Token:
    type: TokenType
    lexeme: str
    literal: float | str | None
    offset: int

    # line and col # in the source
    line_num: int | None = None
    col_num: int | None = None

    def __str__(self) -> str:
        desc = ""
        if self.literal is not None:
            desc = f"[{self.literal}] "
        elif self.type == TokenType.IDENTIFIER:
            desc = f"[{self.lexeme}] "
        return f"{self.type.name} {desc}({self.line_num}:{self.col_num})"


class InterpreterError(Exception):
    def __init__(self, msg: str, line_num: int, col_num: int):
        super().__init__(msg)
        self._line_num = line_num
        self._col_num = col_num

    def get_line_info(self) -> tuple[int, int]:
        return self._line_num, self._col_num
