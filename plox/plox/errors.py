from abc import ABC, abstractmethod
from collections.abc import Iterable

from plox.common import Token


class LoxError(ABC, Exception):
    @abstractmethod
    # returns an iterable of line/col number pairs
    def get_line_info(self) -> Iterable[tuple[int, int]]:
        raise NotImplementedError()


class ScannerError(LoxError):
    def __init__(self, msg: str, source: str, offset: int):
        self._line_pos = self._extract_line_pos(source, offset)
        super().__init__(msg)

    def get_line_info(self) -> Iterable[tuple[int, int]]:
        return (self._line_pos,)

    @staticmethod
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


class _LoxErrorFromTokens(LoxError):
    def __init__(self, msg: str, *tokens: Token) -> None:
        self._tokens = list(tokens)
        super().__init__(msg)

    @property
    def tokens(self) -> list[Token]:
        return self._tokens

    def get_line_info(self) -> Iterable[tuple[int, int]]:
        line_info: list[tuple[int, int]] = []
        for token in self._tokens:
            assert token.line_num is not None
            assert token.col_num is not None
            line_info.append((token.line_num, token.col_num))

        return line_info


class ParserError(_LoxErrorFromTokens):
    pass


class ResolverError(_LoxErrorFromTokens):
    pass


class InterpreterError(_LoxErrorFromTokens):
    pass
