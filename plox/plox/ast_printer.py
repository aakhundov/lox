from plox.ast.expr import Expr, Grouping, Binary, Unary, Literal


class AstPrinter(Expr.Visitor[str]):
    def print(self, e: Expr) -> str:
        return e.accept(self)

    def visit_grouping(self, e: Grouping) -> str:
        return self._parens("grouping", e.expression)

    def visit_binary(self, e: Binary) -> str:
        return self._parens(e.operator.lexeme, e.left, e.right)

    def visit_unary(self, e: Unary) -> str:
        return self._parens(e.operator.lexeme, e.right)

    def visit_literal(self, e: Literal) -> str:
        val = e.value
        if isinstance(val, bool):
            return str(val).lower()
        if isinstance(val, float) and val.is_integer():
            return str(int(val))
        if val is None:
            return "nil"
        return str(val)

    def _parens(self, head: str, *es: Expr) -> str:
        exprs = " ".join(e.accept(self) for e in es)
        return f"({head} {exprs})"
