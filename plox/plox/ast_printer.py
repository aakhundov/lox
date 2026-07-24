from plox.ast import (
    Stmt,
    Var,
    For,
    If,
    Print,
    While,
    LoopJump,
    Block,
    Expression,
    Expr,
    Assign,
    Conditional,
    Logical,
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

    def visit_for(self, s: For) -> str:
        return self._parens(
            "for",
            s.initializer or Literal(None),
            s.condition or Literal(None),
            s.increment or Literal(None),
            s.body,
        )

    def visit_if(self, s: If) -> str:
        if s.else_branch is None:
            return self._parens("if", s.condition, s.then_branch)
        return self._parens(
            "if",
            s.condition,
            s.then_branch,
            s.else_branch,
        )

    def visit_print(self, s: Print) -> str:
        return self._parens("print", *s.expressions)

    def visit_while(self, s: While) -> str:
        return self._parens("while", s.condition, s.body)

    def visit_loopjump(self, s: LoopJump) -> str:
        return f"({s.statement.type.name.lower()})"

    def visit_block(self, s: Block) -> str:
        return self._parens("blk", *s.statements)

    def visit_expression(self, s: Expression) -> str:
        return self._parens("exp", s.expression)

    def visit_assign(self, e: Assign) -> str:
        return self._parens(f"= {e.name.lexeme}", e.value)

    def visit_conditional(self, e: Conditional) -> str:
        return self._parens(
            "?:",
            e.condition,
            e.then_expression,
            e.else_expression,
        )

    def visit_logical(self, e: Logical) -> str:
        return self._parens(e.operator.lexeme, e.left, e.right)

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
