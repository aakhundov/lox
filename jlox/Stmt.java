package jlox;

import java.util.List;

abstract class Stmt {
  interface Visitor<R> {
    R visitBlock(Block stmt);
    R visitClass(Class stmt);
    R visitExpression(Expression stmt);
    R visitFunction(Function stmt);
    R visitIf(If stmt);
    R visitLoopEvent(LoopEvent stmt);
    R visitPrint(Print stmt);
    R visitReturn(Return stmt);
    R visitVar(Var stmt);
    R visitWhile(While stmt);
  }

  abstract <R> R accept(Visitor<R> visitor);

  static class Block extends Stmt {
    final List<Stmt> statements;

    Block(List<Stmt> statements) {
      this.statements = statements;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitBlock(this);
    }
  }

  static class Class extends Stmt {
    final Token name;
    final Expr.Variable superclass;
    final List<Stmt.Function> methods;

    Class(Token name, Expr.Variable superclass, List<Stmt.Function> methods) {
      this.name = name;
      this.superclass = superclass;
      this.methods = methods;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitClass(this);
    }
  }

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

  static class Function extends Stmt {
    final Token name;
    final List<Token> params;
    final List<Stmt> body;

    Function(Token name, List<Token> params, List<Stmt> body) {
      this.name = name;
      this.params = params;
      this.body = body;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitFunction(this);
    }
  }

  static class If extends Stmt {
    final Expr condition;
    final Stmt thenBranch;
    final Stmt elseBranch;

    If(Expr condition, Stmt thenBranch, Stmt elseBranch) {
      this.condition = condition;
      this.thenBranch = thenBranch;
      this.elseBranch = elseBranch;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitIf(this);
    }
  }

  static class LoopEvent extends Stmt {
    final Token statement;

    LoopEvent(Token statement) {
      this.statement = statement;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitLoopEvent(this);
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

  static class Return extends Stmt {
    final Token keyword;
    final Expr value;

    Return(Token keyword, Expr value) {
      this.keyword = keyword;
      this.value = value;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitReturn(this);
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

  static class While extends Stmt {
    final Expr condition;
    final Stmt body;

    While(Expr condition, Stmt body) {
      this.condition = condition;
      this.body = body;
    }

    @Override
    <R> R accept(Visitor<R> visitor) {
      return visitor.visitWhile(this);
    }
  }
}
