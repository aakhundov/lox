from collections.abc import Generator
from contextlib import contextmanager

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
    Super,
    This,
    Variable,
    Grouping,
)
from plox.common import to_repr


_INDENT_SPACES = 2
_INDENT_CACHE = [" " * _INDENT_SPACES * i for i in range(20)]


class CodePrinter(
    Stmt.Visitor[None],
    Expr.Visitor[str],
):
    def __init__(self) -> None:
        self._lines: list[str] = []
        self._indent_level = 0

    def print(self, node: Expr | Stmt | Program) -> str:
        self._lines.clear()
        self._indent_level = 0

        if isinstance(node, Expr):
            return self._str(node)
        elif isinstance(node, Stmt):
            statements: tuple[Stmt, ...] = (node,)
        else:
            statements = node.statements

        for statement in statements:
            self._print(statement)

        assert self._indent_level == 0

        self._remove_trailing_line_breaks()
        return "".join(self._lines)

    def visit_class(self, s: Class) -> None:
        if s.superclass is not None:
            names = f"{s.name.lexeme} < {s.superclass.name.lexeme}"
        else:
            names = s.name.lexeme

        self._line(f"class {names} {{")
        with self._indent():
            for m in s.methods:
                self._print_function(m, keyword=False, name=True)
        self._line("}")

    def visit_function(self, s: Function) -> None:
        self._print_function(s, keyword=True, name=True)

    def visit_var(self, s: Var) -> None:
        self._line(self._generate_var(s))

    def visit_for(self, s: For) -> None:
        init = " ;"
        if isinstance(s.initializer, Var):
            init = self._generate_var(s.initializer)
        elif isinstance(s.initializer, Expression):
            init = self._generate_expression(s.initializer)

        cond = self._str(s.condition) if s.condition is not None else ""
        inc = self._str(s.increment) if s.increment is not None else ""

        # init already contains a ';'
        self._line(f"for ({init} {cond}; {inc})", break_=False)
        self._print_hanging(s.body)

    def visit_if(self, s: If) -> None:
        cond = self._str(s.condition)
        self._line(f"if ({cond})", break_=False)
        self._print_hanging(s.then_branch)

        if s.else_branch is not None:
            self._line("else", break_=False)
            self._print_hanging(s.else_branch)

    def visit_print(self, s: Print) -> None:
        exprs = [self._str(e) for e in s.expressions]
        self._line(f"print {', '.join(exprs)};")

    def visit_return(self, s: Return) -> None:
        if s.value is not None:
            value = self._str(s.value)
            self._line(f"return {value};")
        else:
            self._line("return;")

    def visit_while(self, s: While) -> None:
        cond = self._str(s.condition)
        self._line(f"while ({cond})", break_=False)
        self._print_hanging(s.body)

    def visit_loopjump(self, s: LoopJump) -> None:
        self._line(f"{s.keyword.lexeme};")

    def visit_block(self, s: Block) -> None:
        self._print_block(s.statements)

    def visit_expression(self, s: Expression) -> None:
        self._line(self._generate_expression(s))

    def visit_assign(self, e: Assign) -> str:
        value = self._str(e.value)
        return f"{e.name.lexeme} = {value}"

    def visit_set(self, e: Set) -> str:
        object = self._str(e.object)
        value = self._str(e.value)
        return f"{object}.{e.name.lexeme} = {value}"

    def visit_conditional(self, e: Conditional) -> str:
        cond = self._str(e.condition)
        then_expr = self._str(e.then_expression)
        else_expr = self._str(e.else_expression)
        return f"{cond} ? {then_expr} : {else_expr}"

    def visit_logical(self, e: Logical) -> str:
        left = self._str(e.left)
        right = self._str(e.right)
        return f"{left} {e.operator.lexeme} {right}"

    def visit_binary(self, e: Binary) -> str:
        left = self._str(e.left)
        right = self._str(e.right)
        return f"{left} {e.operator.lexeme} {right}"

    def visit_unary(self, e: Unary) -> str:
        right = self._str(e.right)
        return f"{e.operator.lexeme}{right}"  # no space

    def visit_call(self, e: Call) -> str:
        callee = self._str(e.callee)
        args = [self._str(arg) for arg in e.arguments]
        return f"{callee}({', '.join(args)})"

    def visit_get(self, e: Get) -> str:
        object = self._str(e.object)
        return f"{object}.{e.name.lexeme}"

    def visit_literal(self, e: Literal) -> str:
        return to_repr(e.value)

    def visit_super(self, e: Super) -> str:
        return f"{e.keyword.lexeme}.{e.method.lexeme}"

    def visit_this(self, e: This) -> str:
        return e.keyword.lexeme

    def visit_variable(self, e: Variable) -> str:
        return e.name.lexeme

    def visit_grouping(self, e: Grouping) -> str:
        expr = self._str(e.expression)
        return f"({expr})"

    def _generate_var(self, s: Var) -> str:
        if s.initializer is not None:
            init = self._str(s.initializer)
            return f"var {s.name.lexeme} = {init};"
        else:
            return f"var {s.name.lexeme};"

    def _generate_expression(self, s: Expression) -> str:
        expr = self._str(s.expression)
        return f"{expr};"

    def _print_function(
        self,
        s: Function,
        *,
        keyword: bool,
        name: bool,
    ) -> None:
        if keyword:
            if name:
                prefix = f"fun {s.name.lexeme}"
            else:
                prefix = "fun "
        else:
            if name:
                prefix = s.name.lexeme
            else:
                prefix = ""

        params = ", ".join(p.lexeme for p in s.parameters)
        self._line(f"{prefix}({params}) ", break_=False)
        self._print_block(s.body, hanging=True)

    def _print_hanging(self, s: Stmt) -> None:
        if isinstance(s, Block):
            self._append(" ")  # space before '{'
            self._print_block(s.statements, hanging=True)
        else:
            self._append("\n")  # line break
            with self._indent():
                self._print(s)

    def _print_block(
        self,
        statements: list[Stmt],
        *,
        hanging: bool = False,
    ) -> None:
        if hanging:
            self._append("{\n")  # no indent
        else:
            self._line("{")

        with self._indent():
            for statement in statements:
                self._print(statement)

        self._line("}")

    def _print(self, s: Stmt) -> None:
        s.accept(self)

    def _str(self, e: Expr) -> str:
        return e.accept(self)

    @contextmanager
    def _indent(self, *, increment: int = 1) -> Generator[None]:
        try:
            self._indent_level += increment
            yield
        finally:
            self._indent_level -= increment

    def _line(self, line: str, *, break_: bool = True) -> None:
        if self._indent_level < len(_INDENT_CACHE):
            prefix = _INDENT_CACHE[self._indent_level]
        else:
            prefix = " " * _INDENT_SPACES * self._indent_level

        if break_:
            self._lines.extend((prefix, line, "\n"))
        else:
            self._lines.extend((prefix, line))

    def _append(self, s: str) -> None:
        self._lines.append(s)

    def _remove_trailing_line_breaks(self) -> None:
        while self._lines:
            if self._lines[-1] in ("\n", ""):
                self._lines.pop()
            elif self._lines[-1].endswith("\n"):
                self._lines[-1] = self._lines[-1].rstrip("\n")
            else:
                break
