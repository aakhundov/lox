# THIS FILE IS AUTO-GENERATED: DON'T EDIT BY HAND
# to-regenerate: `python plox/tools/generate_ast.py`

from abc import ABC, abstractmethod
from dataclasses import dataclass

from plox.common import Token, LoxValue


class Stmt(ABC):
    class Visitor[R](ABC):
        @abstractmethod
        def visit_var(self, s: "Var") -> R: ...
        @abstractmethod
        def visit_for(self, s: "For") -> R: ...
        @abstractmethod
        def visit_if(self, s: "If") -> R: ...
        @abstractmethod
        def visit_print(self, s: "Print") -> R: ...
        @abstractmethod
        def visit_while(self, s: "While") -> R: ...
        @abstractmethod
        def visit_block(self, s: "Block") -> R: ...
        @abstractmethod
        def visit_expression(self, s: "Expression") -> R: ...

    @abstractmethod
    def accept[R](self, visitor: Visitor[R]) -> R: ...


@dataclass(frozen=True)
class Var(Stmt):
    name: Token
    initializer: "Expr | None"

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_var(self)


@dataclass(frozen=True)
class For(Stmt):
    initializer: Stmt | None
    condition: "Expr | None"
    increment: "Expr | None"
    body: Stmt

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_for(self)


@dataclass(frozen=True)
class If(Stmt):
    condition: "Expr"
    then_branch: Stmt
    else_branch: Stmt | None

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_if(self)


@dataclass(frozen=True)
class Print(Stmt):
    expressions: list["Expr"]

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_print(self)


@dataclass(frozen=True)
class While(Stmt):
    condition: "Expr"
    body: Stmt

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_while(self)


@dataclass(frozen=True)
class Block(Stmt):
    statements: list[Stmt]

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_block(self)


@dataclass(frozen=True)
class Expression(Stmt):
    expression: "Expr"

    def accept[R](self, visitor: Stmt.Visitor[R]) -> R:
        return visitor.visit_expression(self)


class Expr(ABC):
    class Visitor[R](ABC):
        @abstractmethod
        def visit_assign(self, e: "Assign") -> R: ...
        @abstractmethod
        def visit_conditional(self, e: "Conditional") -> R: ...
        @abstractmethod
        def visit_logical(self, e: "Logical") -> R: ...
        @abstractmethod
        def visit_binary(self, e: "Binary") -> R: ...
        @abstractmethod
        def visit_unary(self, e: "Unary") -> R: ...
        @abstractmethod
        def visit_literal(self, e: "Literal") -> R: ...
        @abstractmethod
        def visit_variable(self, e: "Variable") -> R: ...
        @abstractmethod
        def visit_grouping(self, e: "Grouping") -> R: ...

    @abstractmethod
    def accept[R](self, visitor: Visitor[R]) -> R: ...


@dataclass(frozen=True)
class Assign(Expr):
    name: Token
    value: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_assign(self)


@dataclass(frozen=True)
class Conditional(Expr):
    condition: Expr
    then_expression: Expr
    else_expression: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_conditional(self)


@dataclass(frozen=True)
class Logical(Expr):
    left: Expr
    operator: Token
    right: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_logical(self)


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
    value: LoxValue

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_literal(self)


@dataclass(frozen=True)
class Variable(Expr):
    name: Token

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_variable(self)


@dataclass(frozen=True)
class Grouping(Expr):
    expression: Expr

    def accept[R](self, visitor: Expr.Visitor[R]) -> R:
        return visitor.visit_grouping(self)
