package jlox;

public class AstPrinter implements Expr.Visitor<String>, Stmt.Visitor<String> {
  String print(Stmt stmt) {
    if (stmt == null) {
      return "nil";
    }
    return stmt.accept(this);
  }

  String print(Expr expr) {
    if (expr == null) {
      return "nil";
    }
    return expr.accept(this);
  }

  @Override
  public String visitAssign(Expr.Assign expr) {
    Expr name = new Expr.Literal(expr.name.lexeme);
    return parenthesize("=", name, expr.value);
  }

  @Override
  public String visitBinary(Expr.Binary expr) {
    return parenthesize(expr.operator.lexeme, expr.left, expr.right);
  }

  @Override
  public String visitGrouping(Expr.Grouping expr) {
    return parenthesize("group", expr.expression);
  }

  @Override
  public String visitLiteral(Expr.Literal expr) {
    if (expr.value instanceof String)
      return "\"" + expr.value.toString() + "\"";
    return expr.value.toString();
  }

  @Override
  public String visitUnary(Expr.Unary expr) {
    return parenthesize(expr.operator.lexeme, expr.right);
  }

  @Override
  public String visitVariable(Expr.Variable expr) {
    return expr.name.lexeme;
  }

  @Override
  public String visitExpression(Stmt.Expression stmt) {
    return parenthesize("stmt", stmt.expression);
  }

  @Override
  public String visitPrint(Stmt.Print stmt) {
    return parenthesize("print", stmt.values.toArray(new Expr[0]));
  }

  @Override
  public String visitBlock(Stmt.Block stmt) {
    return parenthesize("block", stmt.statements.toArray(new Stmt[0]));
  }

  @Override
  public String visitVar(Stmt.Var stmt) {
    Expr name = new Expr.Literal(stmt.name.lexeme);
    if (stmt.initializer != null) {
      return parenthesize("var", name, stmt.initializer);
    }
    return parenthesize("var", name);
  }

  private String parenthesize(String name, Expr... exprs) {
    StringBuilder sb = new StringBuilder();

    sb.append("(").append(name);
    for (Expr expr : exprs) {
      sb.append(" ").append(print(expr));
    }
    sb.append(")");

    return sb.toString();
  }

  private String parenthesize(String name, Stmt... stmts) {
    StringBuilder sb = new StringBuilder();

    sb.append("(").append(name);
    for (Stmt stmt : stmts) {
      sb.append(" ").append(print(stmt));
    }
    sb.append(")");

    return sb.toString();
  }
}
