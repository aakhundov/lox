package jlox;

import java.util.List;

abstract class Stmt {
  interface Visitor<R> {
    R visitExpression(Expression stmt);
    R visitPrint(Print stmt);
    R visitVar(Var stmt);
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
    final List<Expr> values;

    Print(List<Expr> values) {
      this.values = values;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitPrint(this);
    }
  }

  static class Var extends Stmt {
    final Token name;
    final Expr initializer;

    Var(Token name, Expr initializer) {
      this.name = name;
      this.initializer = initializer;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitVar(this);
    }
  }
}
