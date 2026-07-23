from plox.ast import (
    Stmt,
    Var,
    Print,
    Block,
    Expression,
    Expr,
    Assign,
    Binary,
    Unary,
    Literal,
    Variable,
    Grouping,
)
from plox.common import to_str


class AstPrinter(
    Expr.Visitor[str],
    Stmt.Visitor[str],
):
    def print(self, node: Expr | Stmt) -> str:
        return node.accept(self)

    def visit_var(self, s: Var) -> str:
        if s.initializer is None:
            return f"(var {s.name.lexeme})"
        return self._parens(f"var {s.name.lexeme}", s.initializer)

    def visit_print(self, s: Print) -> str:
        return self._parens("print", *s.expressions)

    def visit_block(self, s: Block) -> str:
        return self._parens("blk", *s.statements)

    def visit_expression(self, s: Expression) -> str:
        return self._parens("exp", s.expression)

    def visit_assign(self, e: Assign) -> str:
        return self._parens(f"= {e.name.lexeme}", e.value)

    def visit_binary(self, e: Binary) -> str:
        return self._parens(e.operator.lexeme, e.left, e.right)

    def visit_unary(self, e: Unary) -> str:
        return self._parens(e.operator.lexeme, e.right)

    def visit_literal(self, e: Literal) -> str:
        return f'"{e.value}"' if isinstance(e.value, str) else to_str(e.value)

    def visit_variable(self, e: Variable) -> str:
        return e.name.lexeme

    def visit_grouping(self, e: Grouping) -> str:
        return self._parens("grp", e.expression)

    def _parens(self, head: str, *nodes: Expr | Stmt) -> str:
        if not nodes:
            return f"({head})"
        formatted = " ".join(node.accept(self) for node in nodes)
        return f"({head} {formatted})"
