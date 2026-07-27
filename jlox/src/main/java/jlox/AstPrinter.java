package jlox;

import java.util.ArrayList;
import java.util.List;

// Note: this class is not shown in the book beyond chapter 5, where it
// only handles Binary, Grouping, Literal and Unary expressions. The extra
// visit methods exist so that it keeps implementing the full visitor
// interfaces.
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
  public String visitAssignExpr(Expr.Assign expr) {
    Expr name = new Expr.Variable(expr.name);
    return parenthesize("=", name, expr.value);
  }

  @Override
  public String visitBinaryExpr(Expr.Binary expr) {
    return parenthesize(expr.operator.lexeme, expr.left, expr.right);
  }

  @Override
  public String visitCallExpr(Expr.Call expr) {
    List<Expr> exprs = new ArrayList<>();
    exprs.add(expr.callee);
    exprs.addAll(expr.arguments);
    return parenthesize("call", exprs.toArray());
  }

  @Override
  public String visitGetExpr(Expr.Get expr) {
    return parenthesize("get", expr.object, expr.name);
  }

  @Override
  public String visitGroupingExpr(Expr.Grouping expr) {
    return parenthesize("group", expr.expression);
  }

  @Override
  public String visitLiteralExpr(Expr.Literal expr) {
    if (expr.value == null)
      return "nil";
    if (expr.value instanceof String)
      return "\"" + expr.value.toString() + "\"";
    return expr.value.toString();
  }

  @Override
  public String visitLogicalExpr(Expr.Logical expr) {
    return parenthesize(expr.operator.lexeme, expr.left, expr.right);
  }

  @Override
  public String visitSetExpr(Expr.Set expr) {
    return parenthesize("set", expr.object, expr.name, expr.value);
  }

  @Override
  public String visitSuperExpr(Expr.Super expr) {
    return parenthesize("super", expr.method);
  }

  @Override
  public String visitThisExpr(Expr.This expr) {
    return "this";
  }

  @Override
  public String visitUnaryExpr(Expr.Unary expr) {
    return parenthesize(expr.operator.lexeme, expr.right);
  }

  @Override
  public String visitVariableExpr(Expr.Variable expr) {
    return expr.name.lexeme;
  }

  @Override
  public String visitBlockStmt(Stmt.Block stmt) {
    return parenthesize("block", stmt.statements.toArray());
  }

  @Override
  public String visitClassStmt(Stmt.Class stmt) {
    List<Object> parts = new ArrayList<>();
    parts.add(stmt.name);
    if (stmt.superclass != null) {
      parts.add(stmt.superclass);
    }
    parts.addAll(stmt.methods);
    return parenthesize("class", parts.toArray());
  }

  @Override
  public String visitExpressionStmt(Stmt.Expression stmt) {
    return parenthesize("stmt", stmt.expression);
  }

  @Override
  public String visitFunctionStmt(Stmt.Function stmt) {
    List<String> paramNames = new ArrayList<>();
    for (Token param : stmt.params)
      paramNames.add(param.lexeme);
    String params = "(" + String.join(", ", paramNames) + ")";
    String body = parenthesize("body", stmt.body.toArray());
    return parenthesize("fun", stmt.name.lexeme, params, body);
  }

  @Override
  public String visitIfStmt(Stmt.If stmt) {
    if (stmt.elseBranch == null)
      return parenthesize("if", stmt.condition, stmt.thenBranch);
    return parenthesize("if", stmt.condition, stmt.thenBranch, stmt.elseBranch);
  }

  @Override
  public String visitPrintStmt(Stmt.Print stmt) {
    return parenthesize("print", stmt.expression);
  }

  @Override
  public String visitReturnStmt(Stmt.Return stmt) {
    return parenthesize("return", stmt.value);
  }

  @Override
  public String visitVarStmt(Stmt.Var stmt) {
    Expr name = new Expr.Variable(stmt.name);
    if (stmt.initializer != null) {
      return parenthesize("var", name, stmt.initializer);
    }
    return parenthesize("var", name);
  }

  @Override
  public String visitWhileStmt(Stmt.While stmt) {
    return parenthesize("while", stmt.condition, stmt.body);
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
      } else if (part instanceof Token) {
        sb.append(((Token) part).lexeme);
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
