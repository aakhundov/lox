package jlox;

import java.util.List;

class Interpreter implements Expr.Visitor<Object>, Stmt.Visitor<Void> {
  private class LoopBreak extends RuntimeException {
  }

  private Environment environment = new Environment();

  void interpret(List<Stmt> statements) {
    try {
      for (Stmt statement : statements) {
        execute(statement);
      }
    } catch (RuntimeError error) {
      Lox.runtimeError(error);
    }
  }

  @Override
  public Object visitAssign(Expr.Assign expr) {
    Object value = evaluate(expr.value);
    environment.assign(expr.name, value);
    return value;
  }

  @Override
  public Object visitBinary(Expr.Binary expr) {
    Object left = evaluate(expr.left);
    Object right = evaluate(expr.right);

    switch (expr.operator.type) {
      case PLUS:
        if (left instanceof Double && right instanceof Double)
          return (double) left + (double) right;
        if (left instanceof String && right instanceof String)
          return (String) left + (String) right;

        throw new RuntimeError(expr.operator, "Operands must be two numbers or two strings.");
      case MINUS:
        checkNumberOperands(expr.operator, left, right);
        return (double) left - (double) right;
      case STAR:
        checkNumberOperands(expr.operator, left, right);
        return (double) left * (double) right;
      case SLASH:
        checkNumberOperands(expr.operator, left, right);
        return (double) left / (double) right;
      case GREATER:
        checkNumberOperands(expr.operator, left, right);
        return (double) left > (double) right;
      case GREATER_EQUAL:
        checkNumberOperands(expr.operator, left, right);
        return (double) left >= (double) right;
      case LESS:
        checkNumberOperands(expr.operator, left, right);
        return (double) left < (double) right;
      case LESS_EQUAL:
        checkNumberOperands(expr.operator, left, right);
        return (double) left <= (double) right;
      case EQUAL_EQUAL:
        return isEqual(left, right);
      case BANG_EQUAL:
        return !isEqual(left, right);
      default:
        // Unreachable
        return null;
    }
  }

  @Override
  public Object visitGrouping(Expr.Grouping expr) {
    return evaluate(expr.expression);
  }

  @Override
  public Object visitLiteral(Expr.Literal expr) {
    return expr.value;
  }

  @Override
  public Object visitLogical(Expr.Logical expr) {
    Object left = evaluate(expr.left);

    if (expr.operator.type == TokenType.OR) {
      if (isTruthy(left))
        return left;
    } else {
      if (!isTruthy(left))
        return left;
    }

    return evaluate(expr.right);
  }

  @Override
  public Object visitUnary(Expr.Unary expr) {
    Object right = evaluate(expr.right);

    switch (expr.operator.type) {
      case MINUS:
        checkNumberOperand(expr.operator, right);
        return -(double) right;
      case BANG:
        return !isTruthy(right);
      default:
        // Unreachable
        return null;
    }
  }

  @Override
  public Object visitVariable(Expr.Variable expr) {
    return environment.get(expr.name);
  }

  @Override
  public Void visitBlock(Stmt.Block stmt) {
    executeBlock(stmt.statements, new Environment(environment));
    return null;
  }

  @Override
  public Void visitExpression(Stmt.Expression stmt) {
    evaluate(stmt.expression);
    return null;
  }

  @Override
  public Void visitIf(Stmt.If stmt) {
    if (isTruthy(evaluate(stmt.condition))) {
      execute(stmt.thenBranch);
    } else if (stmt.elseBranch != null) {
      execute(stmt.elseBranch);
    }
    return null;
  }

  @Override
  public Void visitLoopEvent(Stmt.LoopEvent stmt) {
    throw new LoopBreak();
  }

  @Override
  public Void visitPrint(Stmt.Print stmt) {
    for (int i = 0; i < stmt.values.size(); ++i) {
      Object value = evaluate(stmt.values.get(i));
      System.out.print(stringify(value));
      if (i < stmt.values.size() - 1)
        System.out.print(" ");
    }
    System.out.println();
    return null;
  }

  @Override
  public Void visitVar(Stmt.Var stmt) {
    Object value = null;
    if (stmt.initializer != null) {
      value = evaluate(stmt.initializer);
    }

    environment.define(stmt.name.lexeme, value);
    return null;
  }

  @Override
  public Void visitWhile(Stmt.While stmt) {
    while (isTruthy(evaluate(stmt.condition))) {
      try {
        execute(stmt.body);
      } catch (LoopBreak e) {
        break;
      }
    }
    return null;
  }

  private void executeBlock(List<Stmt> statements, Environment environment) {
    Environment previous = this.environment;
    try {
      this.environment = environment;

      for (Stmt statement : statements) {
        execute(statement);
      }
    } finally {
      this.environment = previous;
    }
  }

  private void execute(Stmt stmt) {
    stmt.accept(this);
  }

  private Object evaluate(Expr expr) {
    return expr.accept(this);
  }

  private String stringify(Object value) {
    if (value == null)
      return "nil";

    if (value instanceof Double) {
      double number = (double) value;
      double rounded = Math.round(number);
      if (Math.abs(number - rounded) < 1e-8)
        return Integer.valueOf((int) rounded).toString();
      number = Math.round(number * 1e12) / 1e12;
      return Double.valueOf(number).toString();
    }

    return value.toString();
  }

  private boolean isTruthy(Object value) {
    if (value == null)
      return false;
    if (value instanceof Boolean)
      return (boolean) value;
    return true;
  }

  private boolean isEqual(Object a, Object b) {
    if (a == null && b == null)
      return true;
    if (a == null)
      return false;
    return a.equals(b);
  }

  private void checkNumberOperand(Token operator, Object operand) {
    if (operand instanceof Double)
      return;

    throw new RuntimeError(operator, "Operand must be a number.");
  }

  private void checkNumberOperands(Token operator, Object left, Object right) {
    if (left instanceof Double && right instanceof Double)
      return;

    throw new RuntimeError(operator, "Operands must be numbers.");
  }
}
