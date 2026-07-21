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

    # source position
    offset: int | None = None
    line_num: int | None = None
    col_num: int | None = None

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


class LoxError(Exception):
    def __init__(self, msg: str, line_num: int, col_num: int):
        super().__init__(msg)
        self._line_num = line_num
        self._col_num = col_num

    def get_line_info(self) -> tuple[int, int]:
        return self._line_num, self._col_num


class LoxErrorFromToken(LoxError):
    def __init__(self, msg: str, token: Token):
        assert token.line_num is not None
        assert token.col_num is not None
        super().__init__(msg, token.line_num, token.col_num)


class ScannerError(LoxError):
    def __init__(self, msg: str, source: str, offset: int):
        line_num, col_num = self._get_position(source, offset)
        super().__init__(msg, line_num, col_num)

    @staticmethod
    def _get_position(source: str, offset: int) -> tuple[int, int]:
        line_num, col_num = 0, 0
        for i, c in enumerate(source):
            if i == offset:
                break

            if c == "\n":
                line_num += 1
                col_num = 0
            else:
                col_num += 1

        return line_num + 1, col_num + 1


class ParserError(LoxErrorFromToken):
    pass


class InterpreterError(LoxErrorFromToken):
    pass


LoxValue = bool | float | str | None


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
