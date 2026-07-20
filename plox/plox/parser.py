from collections.abc import Callable
from typing import NoReturn

from plox.common import Token, TokenType as TT, LoxErrorFromToken
from plox.ast import Expr, Grouping, Binary, Unary, Literal


class ParserError(LoxErrorFromToken):
    pass


class Parser:
    def __init__(self, tokens: list[Token]):
        self._tokens = tokens
        self._current = 0

    def parse(self) -> Expr:
        return self._expression()

    def _expression(self) -> Expr:
        return self._equality()

    def _equality(self) -> Expr:
        return self._left_fold_binary(
            self._comparison,
            (
                TT.EQUAL_EQUAL,
                TT.BANG_EQUAL,
            ),
        )

    def _comparison(self) -> Expr:
        return self._left_fold_binary(
            self._term,
            (
                TT.LESS,
                TT.LESS_EQUAL,
                TT.GREATER,
                TT.GREATER_EQUAL,
            ),
        )

    def _term(self) -> Expr:
        return self._left_fold_binary(
            self._factor,
            (
                TT.PLUS,
                TT.MINUS,
            ),
        )

    def _factor(self) -> Expr:
        return self._left_fold_binary(
            self._unary,
            (
                TT.STAR,
                TT.SLASH,
            ),
        )

    def _unary(self) -> Expr:
        if self._match(TT.BANG, TT.MINUS):
            operator = self._previous()
            right = self._unary()
            return Unary(operator, right)

        return self._primary()

    def _primary(self) -> Expr:
        if self._match(TT.FALSE):
            return Literal(False)
        if self._match(TT.TRUE):
            return Literal(True)
        if self._match(TT.NIL):
            return Literal(None)

        if self._match(TT.NUMBER, TT.STRING):
            return Literal(self._previous().literal)

        if self._match(TT.LEFT_PAREN):
            expr = self._expression()
            if not self._match(TT.RIGHT_PAREN):
                self._raise("Expected ')' after expression")
            return Grouping(expr)

        self._raise("Expected expression")

    def _left_fold_binary(
        self,
        sub_expr: Callable[[], Expr],
        operators: tuple[TT, ...],
    ) -> Expr:
        left = sub_expr()
        while self._match(*operators):
            operator = self._previous()
            right = sub_expr()
            left = Binary(left, operator, right)

        return left

    def _match(self, *types: TT) -> bool:
        for type_ in types:
            if self._check(type_):
                self._advance()
                return True
        return False

    def _check(self, type_: TT) -> bool:
        return self._peek().type == type_

    def _advance(self) -> Token:
        if not self._is_at_end():
            self._current += 1
        return self._previous()

    def _is_at_end(self) -> bool:
        return self._peek().type == TT.EOF

    def _peek(self) -> Token:
        return self._tokens[self._current]

    def _previous(self) -> Token:
        return self._tokens[self._current - 1]

    def _raise(self, msg: str, token: Token | None = None) -> NoReturn:
        if token is None:
            token = self._peek()
        raise ParserError(msg, token)
