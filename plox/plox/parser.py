from collections.abc import Callable
from typing import NoReturn

from plox.ast import (
    Expr,
    Grouping,
    Binary,
    Unary,
    Literal,
    Stmt,
    Print,
    Expression,
)
from plox.common import Token, TokenType as TT, LoxErrorFromToken


class ParserError(LoxErrorFromToken):
    pass


class Parser:
    def __init__(self, tokens: list[Token]):
        self._tokens = tokens
        self._current = 0

    def parse(self) -> list[Stmt]:
        statements: list[Stmt] = []
        while not self._is_at_end():
            statements.append(self._statement())
        return statements

    def _statement(self) -> Stmt:
        if self._match(TT.PRINT):
            return self._print_stmt()

        # no prefix match: expression statement
        return self._expression_stmt()

    def _print_stmt(self) -> Stmt:
        expressions = [self._expression()]
        while self._match(TT.COMMA):
            expressions.append(self._expression())

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after values",
        )

        return Print(expressions)

    def _expression_stmt(self) -> Stmt:
        expression = self._expression()

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after expression",
        )

        return Expression(expression)

    def _expression(self) -> Expr:
        return self._equality_expr()

    def _equality_expr(self) -> Expr:
        return self._left_fold_binary(
            self._comparison_expr,
            (
                TT.EQUAL_EQUAL,
                TT.BANG_EQUAL,
            ),
        )

    def _comparison_expr(self) -> Expr:
        return self._left_fold_binary(
            self._term_expr,
            (
                TT.LESS,
                TT.LESS_EQUAL,
                TT.GREATER,
                TT.GREATER_EQUAL,
            ),
        )

    def _term_expr(self) -> Expr:
        return self._left_fold_binary(
            self._factor_expr,
            (
                TT.PLUS,
                TT.MINUS,
            ),
        )

    def _factor_expr(self) -> Expr:
        return self._left_fold_binary(
            self._unary_expr,
            (
                TT.STAR,
                TT.SLASH,
            ),
        )

    def _unary_expr(self) -> Expr:
        if self._match(TT.BANG, TT.MINUS):
            operator = self._previous()
            right = self._unary_expr()
            return Unary(operator, right)

        return self._primary_expr()

    def _primary_expr(self) -> Expr:
        if self._match(TT.FALSE):
            return Literal(False)
        if self._match(TT.TRUE):
            return Literal(True)
        if self._match(TT.NIL):
            return Literal(None)

        if self._match(TT.NUMBER, TT.STRING):
            return Literal(self._previous().literal)

        if self._match(TT.LEFT_PAREN):
            expression = self._expression()
            self._consume(
                TT.RIGHT_PAREN,
                "Expected ')' after expression",
            )
            return Grouping(expression)

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

    def _consume(self, type_: TT, error_msg: str) -> Token:
        if self._check(type_):
            return self._advance()
        self._raise(error_msg)

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
