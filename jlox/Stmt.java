package jlox;

abstract class Stmt {
  interface Visitor<R> {
    R visitExpression(Expression stmt);
    R visitPrint(Print stmt);
  }

  abstract <R> R accept(Visitor<R> visitor);

  static class Expression extends Stmt {
    final Expr expression;

    Expression(Expr expression) {
      this.expression = expression;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitExpression(this);
    }
  }

  static class Print extends Stmt {
    final Expr value;

    Print(Expr value) {
      this.value = value;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitPrint(this);
    }
  }
}
