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
  public String visitBlock(Stmt.Block stmt) {
    return parenthesize("block", stmt.statements.toArray());
  }

  @Override
  public String visitExpression(Stmt.Expression stmt) {
    return parenthesize("stmt", stmt.expression);
  }

  @Override
  public String visitIf(Stmt.If stmt) {
    if (stmt.elseBranch == null)
      return parenthesize("if", stmt.condition, stmt.thenBranch);
    return parenthesize("if", stmt.condition, stmt.thenBranch, stmt.elseBranch);
  }

  @Override
  public String visitPrint(Stmt.Print stmt) {
    return parenthesize("print", stmt.values.toArray());
  }

  @Override
  public String visitVar(Stmt.Var stmt) {
    Expr name = new Expr.Literal(stmt.name.lexeme);
    if (stmt.initializer != null) {
      return parenthesize("var", name, stmt.initializer);
    }
    return parenthesize("var", name);
  }

  private String parenthesize(String name, Object... parts) {
    StringBuilder sb = new StringBuilder();

    sb.append("(").append(name);
    for (Object part : parts) {
      sb.append(" ");
      if (part instanceof Expr) {
        sb.append(print((Expr) part));
      } else if (part instanceof Stmt) {
        sb.append(print((Stmt) part));
      } else if (part == null) {
        sb.append("nil");
      } else {
        sb.append(part.toString());
      }
    }
    sb.append(")");

    return sb.toString();
  }
}
