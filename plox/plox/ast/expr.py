# THIS FILE IS AUTO-GENERATED: DON'T EDIT BY HAND
# to-regenerate: `python plox/tools/generate_ast.py`

from abc import ABC, abstractmethod
from dataclasses import dataclass

from plox.common import Token


class Expr(ABC):
    class Visitor[R](ABC):
        @abstractmethod
        def visit_grouping(self, e: "Grouping") -> R: ...
        @abstractmethod
        def visit_binary(self, e: "Binary") -> R: ...
        @abstractmethod
        def visit_unary(self, e: "Unary") -> R: ...
        @abstractmethod
        def visit_literal(self, e: "Literal") -> R: ...

    @abstractmethod
    def accept[R](self, visitor: Visitor[R]) -> R: ...


@dataclass(frozen=True)
class Grouping(Expr):
    expression: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_grouping(self)


@dataclass(frozen=True)
class Binary(Expr):
    left: Expr
    operator: Token
    right: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_binary(self)


@dataclass(frozen=True)
class Unary(Expr):
    operator: Token
    right: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_unary(self)


@dataclass(frozen=True)
class Literal(Expr):
    value: bool | float | str | None

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_literal(self)
