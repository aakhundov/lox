from typing import NoReturn

from plox.common import (
    InterpreterError,
    Token,
    TokenType as TT,
)


class ScannerError(InterpreterError):
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


class Scanner:
    _KEYWORDS = {
        "and": TT.AND,
        "class": TT.CLASS,
        "else": TT.ELSE,
        "false": TT.FALSE,
        "for": TT.FOR,
        "fun": TT.FUN,
        "if": TT.IF,
        "nil": TT.NIL,
        "or": TT.OR,
        "print": TT.PRINT,
        "return": TT.RETURN,
        "super": TT.SUPER,
        "this": TT.THIS,
        "true": TT.TRUE,
        "var": TT.VAR,
        "while": TT.WHILE,
    }

    def __init__(self, source: str) -> None:
        self._source: str = source
        self._tokens: list[Token] = []
        self._start: int = 0
        self._current: int = 0

    def scan(self) -> list[Token]:
        self._current = 0
        self._tokens.clear()

        while not self._at_end():
            self._start = self._current
            self._scan_token()

        self._start = self._current
        self._add_token(TT.EOF)
        self._add_line_metadata()

        return list(self._tokens)

    def _scan_token(self) -> None:
        c = self._advance()
        if c == "(":
            self._add_token(TT.LEFT_PAREN)
        elif c == ")":
            self._add_token(TT.RIGHT_PAREN)
        elif c == "{":
            self._add_token(TT.LEFT_BRACE)
        elif c == "}":
            self._add_token(TT.RIGHT_BRACE)
        elif c == ",":
            self._add_token(TT.COMMA)
        elif c == ".":
            self._add_token(TT.DOT)
        elif c == "-":
            self._add_token(TT.MINUS)
        elif c == "+":
            self._add_token(TT.PLUS)
        elif c == ";":
            self._add_token(TT.SEMICOLON)
        elif c == "*":
            self._add_token(TT.STAR)
        elif c == "!":
            self._add_token(TT.BANG_EQUAL if self._match("=") else TT.BANG)
        elif c == ">":
            self._add_token(TT.GREATER_EQUAL if self._match("=") else TT.GREATER)
        elif c == "<":
            self._add_token(TT.LESS_EQUAL if self._match("=") else TT.LESS)
        elif c == "=":
            self._add_token(TT.EQUAL_EQUAL if self._match("=") else TT.EQUAL)
        elif c == "/":
            if self._match("/"):
                # ignore // ... comment
                while not self._at_end() and self._peek() != "\n":
                    self._advance()
            elif self._match("*"):
                # ignore /* ... */ comment (nesting not allowed)
                while not self._at_end():
                    if self._peek() == "*" and self._peek_next() == "/":
                        self._advance()  # eat the closing '*'
                        self._advance()  # eat the closing '/'
                        break
                    self._advance()
                else:
                    # the loop finished without breaking
                    self._raise("Unterminated comment")
            else:
                self._add_token(TT.SLASH)
        elif c in (" ", "\t", "\n", "\r"):
            # whitespace
            pass
        elif c == '"':
            self._string()
        elif self._is_digit(c):
            self._number()
        elif self._is_alpha(c):
            self._identifier()
        else:
            self._raise(f"Unexpected character: '{c}'")

    def _string(self) -> None:
        while not self._at_end() and self._peek() != '"':
            self._advance()

        if self._at_end():
            self._raise("Unterminated string")

        self._advance()  # eat the closing '"'

        # omit the quotes in the string literal
        value = self._source[self._start + 1 : self._current - 1]
        self._add_token(TT.STRING, value)

    def _number(self) -> None:
        while self._is_digit(self._peek()):
            self._advance()

        if self._peek() == "." and self._is_digit(self._peek_next()):
            # the number has '.' and more digits
            self._advance()  # eat the '.'
            while self._is_digit(self._peek()):
                self._advance()

        str_value = self._source[self._start : self._current]
        self._add_token(TT.NUMBER, float(str_value))

    def _identifier(self) -> None:
        while self._is_alnum(self._peek()):
            self._advance()

        name = self._source[self._start : self._current]
        if kw_token := self._KEYWORDS.get(name):
            # the identifier is a keyword
            self._add_token(kw_token)
        else:
            self._add_token(TT.IDENTIFIER)

    def _add_token(self, type_: TT, literal: float | str | None = None) -> None:
        lexeme = self._source[self._start : self._current]
        token = Token(type_, lexeme, literal, self._start)
        self._tokens.append(token)

    def _at_end(self) -> bool:
        return self._current >= len(self._source)

    def _advance(self) -> str:
        c = self._source[self._current]
        self._current += 1
        return c

    def _match(self, pattern: str) -> bool:
        if self._at_end():
            return False
        if self._source[self._current] != pattern:
            return False
        self._current += 1
        return True

    def _peek(self) -> str:
        if self._at_end():
            return "\0"
        return self._source[self._current]

    def _peek_next(self) -> str:
        if self._current + 1 >= len(self._source):
            return "\0"
        return self._source[self._current + 1]

    def _is_digit(self, c: str) -> bool:
        return c.isascii() and c.isdigit()

    def _is_alpha(self, c: str) -> bool:
        return (c.isascii() and c.isalpha()) or c == "_"

    def _is_alnum(self, c: str) -> bool:
        return self._is_alpha(c) or self._is_digit(c)

    def _raise(self, msg: str, offset: int | None = None) -> NoReturn:
        if offset is None:
            offset = self._start
        raise ScannerError(msg, self._source, offset)

    def _add_line_metadata(self) -> None:
        token_offsets = {t.offset for t in self._tokens}
        line_meta: dict[int, tuple[int, int]] = {}

        line_num, col_num = 0, 0
        for offset, c in enumerate(self._source):
            if offset in token_offsets:
                line_meta[offset] = (line_num + 1, col_num + 1)

            if c == "\n":
                line_num += 1
                col_num = 0
            else:
                col_num += 1

        # EOF line / col num aren't covered in the loop above
        line_meta[len(self._source)] = (line_num + 1, col_num + 1)

        for token in self._tokens:
            token.line_num, token.col_num = line_meta[token.offset]
