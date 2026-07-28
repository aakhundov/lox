from prompt_toolkit.lexers import Lexer
from typing import Callable, cast, TYPE_CHECKING

from plox.common import Token, TokenType as TT
from plox.scanner import Scanner

if TYPE_CHECKING:
    from prompt_toolkit.document import Document
    from prompt_toolkit.formatted_text import StyleAndTextTuples


# three roles, not one color per token class: the words the
# language reserves, the data the program carries, and the
# punctuation holding them together — names keep the default
# foreground, so they stay the plainest thing on the line
_SYNTAX = "#D2A8FF"  # keywords
_DATA = "#4EC9B0"  # literals, `true`/`false`/`nil` included
_PUNCTUATION = "#79C0FF"  # operators and separators
_NAME = ""  # identifiers: terminal default

_STYLES: dict[TT, str] = {
    **dict.fromkeys(
        (
            TT.VAR,
            TT.FUN,
            TT.CLASS,
            TT.IF,
            TT.ELSE,
            TT.FOR,
            TT.WHILE,
            TT.RETURN,
            TT.BREAK,
            TT.CONTINUE,
            TT.AND,
            TT.OR,
            TT.PRINT,
            TT.THIS,
            TT.SUPER,
        ),
        _SYNTAX,
    ),
    **dict.fromkeys(
        (
            TT.STRING,
            TT.NUMBER,
            TT.TRUE,
            TT.FALSE,
            TT.NIL,
        ),
        _DATA,
    ),
    **dict.fromkeys(
        (
            TT.LEFT_PAREN,
            TT.RIGHT_PAREN,
            TT.LEFT_BRACE,
            TT.RIGHT_BRACE,
            TT.COMMA,
            TT.DOT,
            TT.SEMICOLON,
            TT.QUESTION,
            TT.COLON,
            TT.MINUS,
            TT.PLUS,
            TT.SLASH,
            TT.STAR,
            TT.BANG,
            TT.BANG_EQUAL,
            TT.EQUAL,
            TT.EQUAL_EQUAL,
            TT.GREATER,
            TT.GREATER_EQUAL,
            TT.LESS,
            TT.LESS_EQUAL,
        ),
        _PUNCTUATION,
    ),
    TT.IDENTIFIER: _NAME,
}


def _get_token_types_per_line(
    lines: list[str],
    tokens: list[Token],
) -> list[list[tuple[int, int, TT]]]:
    # list of <list of ordered (start_idx, end_idx, token_type)> for each line
    types_per_line: list[list[tuple[int, int, TT]]] = [[] for i in range(len(lines))]
    for token in tokens:
        length = len(token.lexeme)
        lineno = cast(int, token.line_num) - 1
        start_idx = cast(int, token.col_num) - 1

        if token.type == TT.STRING:
            # strings are multi-line
            while length > 0:
                end_idx = min(start_idx + length, len(lines[lineno]))
                types_per_line[lineno].append((start_idx, end_idx, token.type))

                length -= (end_idx - start_idx) + 1  # (possible) line break
                start_idx = 0
                lineno += 1
        else:
            end_idx = start_idx + length
            types_per_line[lineno].append((start_idx, end_idx, token.type))

    return types_per_line


class LoxLexer(Lexer):
    def lex_document(
        self,
        document: "Document",
    ) -> Callable[[int], "StyleAndTextTuples"]:
        lines = document.lines
        tokens = Scanner(
            document.text,
            ignore_errors=True,
        ).scan()

        types_per_line = _get_token_types_per_line(lines, tokens)
        # _write_debug_info(lines, types_per_line)

        def get_line(lineno: int) -> "StyleAndTextTuples":
            if lineno >= len(lines):
                return []

            line = lines[lineno]
            parts: "StyleAndTextTuples" = []

            last_idx = 0
            # assuming tokens are ordered within the line
            for start_idx, end_idx, tt in types_per_line[lineno]:
                if start_idx > last_idx:
                    # substring between two consecutive tokens
                    parts.append(("", line[last_idx:start_idx]))

                # every token type but EOF is in the _STYLES table
                parts.append((_STYLES.get(tt, _NAME), line[start_idx:end_idx]))
                last_idx = end_idx

            if len(line) > last_idx:
                # dangling substring
                parts.append(("", line[last_idx:]))

            return parts

        return get_line
