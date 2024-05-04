package jlox;

public class Token {
  private String s;

  public Token(String s) {
    this.s = s;
  }

  public String toString() {
    return s;
  }
}
