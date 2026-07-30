from abc import ABC, abstractmethod

from plox.common import Token, LoxValue, Source, Location


def _extract_line_pos(source: str, offset: int) -> tuple[int, int]:
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


class LoxError(ABC, Exception):
    @property
    @abstractmethod
    def locations(self) -> list[Location]: ...


class ScannerError(LoxError):
    def __init__(self, msg: str, source: Source, offset: int):
        line_num, col_num = _extract_line_pos(source.code, offset)
        self._location = source.get_location(line_num, col_num)
        super().__init__(msg)

    @property
    def locations(self) -> list[Location]:
        return [self._location]


class _LoxErrorFromTokens(LoxError):
    def __init__(self, msg: str, *tokens: Token) -> None:
        self._tokens = list(tokens)
        super().__init__(msg)

    @property
    def tokens(self) -> list[Token]:
        return self._tokens

    @property
    def locations(self) -> list[Location]:
        return [token.get_location() for token in self._tokens]


class ParserError(_LoxErrorFromTokens):
    pass


class ResolverError(_LoxErrorFromTokens):
    pass


class InterpreterError(_LoxErrorFromTokens):
    pass


class CallableReturn(Exception):
    def __init__(self, value: LoxValue):
        self._value = value

    @property
    def value(self) -> LoxValue:
        return self._value


class NativeFnError(Exception):
    pass
