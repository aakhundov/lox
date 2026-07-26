from collections.abc import Callable
from typing import NoReturn

from plox.ast import (
    Program,
    Stmt,
    Class,
    Function,
    Var,
    For,
    If,
    Print,
    Return,
    While,
    LoopJump,
    Block,
    Expression,
    Expr,
    Assign,
    Set,
    Conditional,
    Logical,
    Binary,
    Unary,
    Call,
    Get,
    Literal,
    This,
    Variable,
    Grouping,
)
from plox.common import Token, TokenType as TT
from plox.errors import ParserError


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self._tokens = tokens
        self._current = 0
        self._errors: list[ParserError] = []

    def parse(self) -> Program:
        self._current = 0
        self._errors.clear()

        statements: list[Stmt] = []
        while not self._is_at_end():
            try:
                statements.append(self._declaration())
            except ParserError:
                # skip to the next stmt
                self._synchronize()

        if self._errors:
            raise ExceptionGroup("Parser errors", self._errors)

        return Program(tuple(statements))

    def _declaration(self) -> Stmt:
        if self._match(TT.CLASS):
            return self._class()
        if self._match(TT.FUN):
            return self._function("function")
        if self._match(TT.VAR):
            return self._var()

        return self._statement()

    def _class(self) -> Class:
        name = self._consume(
            TT.IDENTIFIER,
            "Expect class name",
        )
        self._consume(
            TT.LEFT_BRACE,
            "Expect '{' before class body",
        )

        methods: list[Function] = []
        while not self._check(TT.RIGHT_BRACE) and not self._is_at_end():
            methods.append(self._function("method"))

        self._consume(
            TT.RIGHT_BRACE,
            "Expect '}' after class body",
        )

        return Class(name, methods)

    def _function(self, kind: str) -> Function:
        name = self._consume(
            TT.IDENTIFIER,
            f"Expect {kind} name",
        )
        self._consume(
            TT.LEFT_PAREN,
            f"Expect '(' after {kind} name",
        )

        parameters: list[Token] = []
        if not self._check(TT.RIGHT_PAREN):
            parameters.append(
                self._consume(
                    TT.IDENTIFIER,
                    "Expect parameter name",
                )
            )
            while self._match(TT.COMMA):
                parameters.append(
                    self._consume(
                        TT.IDENTIFIER,
                        "Expect parameter name",
                    )
                )

        self._consume(
            TT.RIGHT_PAREN,
            f"Expect ')' after {kind} parameters",
        )
        self._consume(
            TT.LEFT_BRACE,
            f"Expect '{{' before {kind} body",
        )

        body = self._parse_block()

        return Function(name, parameters, body)

    def _var(self) -> Var:
        name = self._consume(
            TT.IDENTIFIER,
            "Expect variable name",
        )

        initializer = None
        if self._match(TT.EQUAL):
            initializer = self._expression()

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after variable declaration",
        )

        return Var(name, initializer)

    def _statement(self) -> Stmt:
        if self._match(TT.FOR):
            return self._for()
        if self._match(TT.IF):
            return self._if()
        if self._match(TT.PRINT):
            return self._print()
        if self._match(TT.RETURN):
            return self._return()
        if self._match(TT.WHILE):
            return self._while()
        if self._match(TT.BREAK, TT.CONTINUE):
            return self._loop_jump()
        if self._match(TT.LEFT_BRACE):
            return self._block()

        return self._expression_statement()

    def _for(self) -> For:
        self._consume(
            TT.LEFT_PAREN,
            "Expect '(' after for",
        )

        initializer: Var | Expression | None = None
        if not self._match(TT.SEMICOLON):
            if self._match(TT.VAR):
                initializer = self._var()
            else:
                initializer = self._expression_statement()

        condition = None
        if not self._check(TT.SEMICOLON):
            condition = self._expression()

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after for condition",
        )

        increment = None
        if not self._check(TT.RIGHT_PAREN):
            increment = self._expression()

        self._consume(
            TT.RIGHT_PAREN,
            "Expect ')' after for clauses",
        )

        body = self._statement()

        return For(initializer, condition, increment, body)

    def _if(self) -> If:
        self._consume(
            TT.LEFT_PAREN,
            "Expect '(' after if",
        )

        condition = self._expression()

        self._consume(
            TT.RIGHT_PAREN,
            "Expect ')' after if condition",
        )

        then_branch = self._statement()
        else_branch = self._statement() if self._match(TT.ELSE) else None

        return If(condition, then_branch, else_branch)

    def _print(self) -> Print:
        expressions = [self._expression()]
        while self._match(TT.COMMA):
            expressions.append(self._expression())

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after values",
        )

        return Print(expressions)

    def _return(self) -> Return:
        keyword = self._previous()

        value = None
        if not self._check(TT.SEMICOLON):
            value = self._expression()

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after return value",
        )

        return Return(keyword, value)

    def _while(self) -> While:
        self._consume(
            TT.LEFT_PAREN,
            "Expect '(' after while",
        )

        condition = self._expression()

        self._consume(
            TT.RIGHT_PAREN,
            "Expect ')' after while condition",
        )

        body = self._statement()

        return While(condition, body)

    def _loop_jump(self) -> LoopJump:
        keyword = self._previous()
        kw = keyword.type.name.lower()

        self._consume(
            TT.SEMICOLON,
            f"Expect ';' after {kw}",
        )

        return LoopJump(keyword)

    def _block(self) -> Block:
        statements = self._parse_block()
        return Block(statements)

    def _expression_statement(self) -> Expression:
        expression = self._expression()

        self._consume(
            TT.SEMICOLON,
            "Expect ';' after expression",
        )

        return Expression(expression)

    def _expression(self) -> Expr:
        return self._assignment()

    def _assignment(self) -> Expr:
        expr = self._conditional()

        if self._match(TT.EQUAL):
            equals = self._previous()
            value = self._assignment()  # right-associative

            if isinstance(expr, Variable):
                return Assign(expr.name, value)
            elif isinstance(expr, Get):
                # turn preceding getter into a setter
                return Set(expr.object, expr.name, value)

            # don't raise, as already in a consistent state
            self._error("Invalid assignment target", equals)

        return expr

    def _conditional(self) -> Expr:
        condition = self._or()

        if self._match(TT.QUESTION):
            then_expression = self._expression()

            self._consume(
                TT.COLON,
                "Expect ':' to match ?",
            )

            else_expression = self._conditional()  # right-associative

            return Conditional(
                condition,
                then_expression,
                else_expression,
            )

        return condition

    def _or(self) -> Expr:
        return self._left_fold(
            self._and,
            (TT.OR,),
            type_=Logical,
        )

    def _and(self) -> Expr:
        return self._left_fold(
            self._equality,
            (TT.AND,),
            type_=Logical,
        )

    def _equality(self) -> Expr:
        return self._left_fold(
            self._comparison,
            (
                TT.EQUAL_EQUAL,
                TT.BANG_EQUAL,
            ),
        )

    def _comparison(self) -> Expr:
        return self._left_fold(
            self._term,
            (
                TT.LESS,
                TT.LESS_EQUAL,
                TT.GREATER,
                TT.GREATER_EQUAL,
            ),
        )

    def _term(self) -> Expr:
        return self._left_fold(
            self._factor,
            (
                TT.PLUS,
                TT.MINUS,
            ),
        )

    def _factor(self) -> Expr:
        return self._left_fold(
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

        return self._call()

    def _call(self) -> Expr:
        expr = self._primary()

        while True:
            if self._check(TT.LEFT_PAREN):
                expr = self._finish_call(expr)
            elif self._match(TT.DOT):
                name = self._consume(
                    TT.IDENTIFIER,
                    "Expect property name after '.'",
                )

                expr = Get(expr, name)
            else:
                break

        return expr

    def _primary(self) -> Expr:
        if self._match(TT.FALSE):
            return Literal(False)
        if self._match(TT.TRUE):
            return Literal(True)
        if self._match(TT.NIL):
            return Literal(None)

        if self._match(TT.NUMBER, TT.STRING):
            return Literal(self._previous().literal)
        if self._match(TT.THIS):
            return This(self._previous())
        if self._match(TT.IDENTIFIER):
            return Variable(self._previous())

        if self._match(TT.LEFT_PAREN):
            expression = self._expression()

            self._consume(
                TT.RIGHT_PAREN,
                "Expect ')' after expression",
            )

            return Grouping(expression)

        self._raise("Expect expression")

    def _finish_call(self, callee: Expr) -> Call:
        paren = self._advance()

        arguments: list[Expr] = []
        if not self._check(TT.RIGHT_PAREN):
            arguments.append(self._expression())  # first arg
            while self._match(TT.COMMA):
                arguments.append(self._expression())  # more args

        self._consume(
            TT.RIGHT_PAREN,
            "Expect ')' after call arguments",
        )

        return Call(callee, paren, arguments)

    def _parse_block(self) -> list[Stmt]:
        statements: list[Stmt] = []
        while not self._check(TT.RIGHT_BRACE) and not self._is_at_end():
            statements.append(self._declaration())  # can contain decls

        self._consume(
            TT.RIGHT_BRACE,
            "Expect '}' after block",
        )

        return statements

    def _left_fold(
        self,
        sub_expr: Callable[[], Expr],
        operators: tuple[TT, ...],
        *,
        type_: type[Binary] | type[Logical] = Binary,
    ) -> Expr:
        left = sub_expr()
        while self._match(*operators):
            operator = self._previous()
            right = sub_expr()
            left = type_(left, operator, right)

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
        raise self._error(msg, token)

    def _error(self, msg: str, token: Token | None = None) -> ParserError:
        # all errors must be reported through this method
        # either by calling it directly or via _raise
        if token is None:
            token = self._peek()

        error = ParserError(msg, token)
        self._errors.append(error)
        return error

    def _synchronize(self) -> None:
        self._advance()  # skip the bad token

        while not self._is_at_end():
            if self._previous().type == TT.SEMICOLON:
                # new stmt after ;
                return
            if self._peek().type in (
                TT.CLASS,
                TT.FUN,
                TT.VAR,
                TT.FOR,
                TT.IF,
                TT.WHILE,
                TT.PRINT,
                TT.RETURN,
            ):
                # new decl / stmt by keyword
                return

            self._advance()
