package jlox;

import java.util.ArrayList;
import java.util.List;

public class Scanner {
  private String source;

  public Scanner(String source) {
    this.source = source;
  }

  List<Token> scanTokens() {
    ArrayList<Token> tokens = new ArrayList<Token>();

    // TODO: add actual scanner implementation
    for (String s : source.split(" ")) {
      tokens.add(new Token(s));
    }

    return tokens;
  }
}
