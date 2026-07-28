import pytest

from plox.common import TokenType as TT
from plox.scanner import Scanner, ScannerError

# the scanner always appends this terminal token;
# expectations include it in the test functions
EOF = (TT.EOF, "", None)


@pytest.fixture
def scan_errors(collect_errors):
    """Return a helper that scans `source` expecting failure."""

    def _scan_errors(source):
        return collect_errors(ScannerError, Scanner(source).scan)

    return _scan_errors


@pytest.fixture
def scan():
    """Return a helper that scans source into (type, lexeme, literal) triples.

    The token's `offset` is intentionally dropped so expectations stay robust to
    spacing; when a test cares about offsets, use the raw `Scanner` directly.
    """

    def _scan(source):
        return [
            (token.type, token.lexeme, token.literal)
            for token in Scanner(source).scan()
        ]

    return _scan


@pytest.mark.parametrize(
    "source, token_type",
    [
        ("(", TT.LEFT_PAREN),
        (")", TT.RIGHT_PAREN),
        ("{", TT.LEFT_BRACE),
        ("}", TT.RIGHT_BRACE),
        (",", TT.COMMA),
        (".", TT.DOT),
        ("-", TT.MINUS),
        ("+", TT.PLUS),
        (";", TT.SEMICOLON),
        ("*", TT.STAR),
        ("/", TT.SLASH),
        ("!", TT.BANG),
        ("=", TT.EQUAL),
        ("<", TT.LESS),
        (">", TT.GREATER),
        ("?", TT.QUESTION),
        (":", TT.COLON),
    ],
)
def test_single_character_tokens(scan, source, token_type):
    assert scan(source) == [(token_type, source, None), EOF]


@pytest.mark.parametrize(
    "source, token_type",
    [
        ("!=", TT.BANG_EQUAL),
        ("==", TT.EQUAL_EQUAL),
        ("<=", TT.LESS_EQUAL),
        (">=", TT.GREATER_EQUAL),
    ],
)
def test_two_character_operators(scan, source, token_type):
    assert scan(source) == [(token_type, source, None), EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        ("===", [(TT.EQUAL_EQUAL, "==", None), (TT.EQUAL, "=", None)]),
        ("!=!", [(TT.BANG_EQUAL, "!=", None), (TT.BANG, "!", None)]),
        ("<=<", [(TT.LESS_EQUAL, "<=", None), (TT.LESS, "<", None)]),
        (">>=", [(TT.GREATER, ">", None), (TT.GREATER_EQUAL, ">=", None)]),
        # whitespace splits a two-character operator apart
        ("! =", [(TT.BANG, "!", None), (TT.EQUAL, "=", None)]),
        ("< =", [(TT.LESS, "<", None), (TT.EQUAL, "=", None)]),
    ],
)
def test_maximal_munch(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        ("// just a comment", []),
        ("//", []),
        ("/", [(TT.SLASH, "/", None)]),
        (
            "a / b",
            [
                (TT.IDENTIFIER, "a", None),
                (TT.SLASH, "/", None),
                (TT.IDENTIFIER, "b", None),
            ],
        ),
        ("1 // comment\n2", [(TT.NUMBER, "1", 1.0), (TT.NUMBER, "2", 2.0)]),
        ("// comment\n", []),
    ],
)
def test_line_comments(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        ("/* comment */", []),
        ("/**/", []),
        ("/***/", []),  # the content is a single '*'
        ("/* spans\ntwo lines */", []),
        ("/* a */ 1", [(TT.NUMBER, "1", 1.0)]),
        (
            "1 /* x */ + 2",
            [
                (TT.NUMBER, "1", 1.0),
                (TT.PLUS, "+", None),
                (TT.NUMBER, "2", 2.0),
            ],
        ),
        # nesting is not allowed: the first */ closes the comment
        (
            "/* /* nested */ tail */",
            [
                (TT.IDENTIFIER, "tail", None),
                (TT.STAR, "*", None),
                (TT.SLASH, "/", None),
            ],
        ),
        # a single */ closes the whole thing even with an inner /* (no nesting)
        ("/* outer /* inner */", []),
    ],
)
def test_block_comments(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        ("", []),
        ("   ", []),
        (" \t\r\n ", []),
        ("1\t2", [(TT.NUMBER, "1", 1.0), (TT.NUMBER, "2", 2.0)]),
        ("\n\n1", [(TT.NUMBER, "1", 1.0)]),
        ("1  ", [(TT.NUMBER, "1", 1.0)]),
    ],
)
def test_whitespace_is_ignored(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, value",
    [
        ('"hello"', "hello"),
        ('""', ""),
        ('"with spaces"', "with spaces"),
        ('"123 and + var"', "123 and + var"),
        ('"line1\nline2"', "line1\nline2"),
        ('"// not a comment"', "// not a comment"),
    ],
)
def test_string_literals(scan, source, value):
    assert scan(source) == [(TT.STRING, source, value), EOF]


@pytest.mark.parametrize(
    "source, value",
    [
        ("0", 0.0),
        ("123", 123.0),
        ("12.5", 12.5),
        ("3.14159", 3.14159),
        ("007", 7.0),
    ],
)
def test_number_literals(scan, source, value):
    assert scan(source) == [(TT.NUMBER, source, value), EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        ("12.34", [(TT.NUMBER, "12.34", 12.34)]),
        ("123.", [(TT.NUMBER, "123", 123.0), (TT.DOT, ".", None)]),
        (".5", [(TT.DOT, ".", None), (TT.NUMBER, "5", 5.0)]),
        (
            "1.2.3",
            [
                (TT.NUMBER, "1.2", 1.2),
                (TT.DOT, ".", None),
                (TT.NUMBER, "3", 3.0),
            ],
        ),
        (
            "1.foo",
            [
                (TT.NUMBER, "1", 1.0),
                (TT.DOT, ".", None),
                (TT.IDENTIFIER, "foo", None),
            ],
        ),
    ],
)
def test_number_dot_boundaries(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, token_type",
    [
        ("and", TT.AND),
        ("break", TT.BREAK),
        ("class", TT.CLASS),
        ("continue", TT.CONTINUE),
        ("else", TT.ELSE),
        ("false", TT.FALSE),
        ("for", TT.FOR),
        ("fun", TT.FUN),
        ("if", TT.IF),
        ("nil", TT.NIL),
        ("or", TT.OR),
        ("print", TT.PRINT),
        ("return", TT.RETURN),
        ("super", TT.SUPER),
        ("this", TT.THIS),
        ("true", TT.TRUE),
        ("var", TT.VAR),
        ("while", TT.WHILE),
    ],
)
def test_keywords(scan, source, token_type):
    assert scan(source) == [(token_type, source, None), EOF]


@pytest.mark.parametrize(
    "source",
    ["foo", "bar123", "_", "_x", "foo_bar", "camelCase", "a1b2c3"],
)
def test_identifiers(scan, source):
    assert scan(source) == [(TT.IDENTIFIER, source, None), EOF]


@pytest.mark.parametrize(
    "source",
    [
        "orchid",
        "classy",
        "ifx",
        "forlorn",
        "variable",
        "breaks",
        "continued",
        "VAR",
        "Var",
        "True",
        "NIL",
    ],
)
def test_keyword_lookalikes_are_identifiers(scan, source):
    assert scan(source) == [(TT.IDENTIFIER, source, None), EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        (
            "1+2",
            [
                (TT.NUMBER, "1", 1.0),
                (TT.PLUS, "+", None),
                (TT.NUMBER, "2", 2.0),
            ],
        ),
        (
            "var x = 10;",
            [
                (TT.VAR, "var", None),
                (TT.IDENTIFIER, "x", None),
                (TT.EQUAL, "=", None),
                (TT.NUMBER, "10", 10.0),
                (TT.SEMICOLON, ";", None),
            ],
        ),
        (
            'print "hi";',
            [
                (TT.PRINT, "print", None),
                (TT.STRING, '"hi"', "hi"),
                (TT.SEMICOLON, ";", None),
            ],
        ),
        (
            "a.b(c)",
            [
                (TT.IDENTIFIER, "a", None),
                (TT.DOT, ".", None),
                (TT.IDENTIFIER, "b", None),
                (TT.LEFT_PAREN, "(", None),
                (TT.IDENTIFIER, "c", None),
                (TT.RIGHT_PAREN, ")", None),
            ],
        ),
        (
            '"a""b"',
            [(TT.STRING, '"a"', "a"), (TT.STRING, '"b"', "b")],
        ),
        (
            '("x")',
            [
                (TT.LEFT_PAREN, "(", None),
                (TT.STRING, '"x"', "x"),
                (TT.RIGHT_PAREN, ")", None),
            ],
        ),
        (
            "123abc",
            [(TT.NUMBER, "123", 123.0), (TT.IDENTIFIER, "abc", None)],
        ),
        # a conditional's '?' and ':' scan apart with no surrounding whitespace
        (
            "a?b:c",
            [
                (TT.IDENTIFIER, "a", None),
                (TT.QUESTION, "?", None),
                (TT.IDENTIFIER, "b", None),
                (TT.COLON, ":", None),
                (TT.IDENTIFIER, "c", None),
            ],
        ),
    ],
)
def test_multi_token_sequences(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, offsets",
    [
        ("1 + 22", [0, 2, 4, 6]),
        # offsets are absolute indices that
        # keep counting across newlines
        ("1 +\n2", [0, 2, 4, 5]),
        ("foo", [0, 3]),
        ("", [0]),
    ],
)
def test_token_offsets(source, offsets):
    assert [token.offset for token in Scanner(source).scan()] == offsets


@pytest.mark.parametrize(
    "source, expected",
    [
        (
            """\
            fun fib(n) {
            if (n < 2) return n;
            return fib(n - 1) + fib(n - 2);
            }
            """,
            [
                (TT.FUN, "fun", None),
                (TT.IDENTIFIER, "fib", None),
                (TT.LEFT_PAREN, "(", None),
                (TT.IDENTIFIER, "n", None),
                (TT.RIGHT_PAREN, ")", None),
                (TT.LEFT_BRACE, "{", None),
                (TT.IF, "if", None),
                (TT.LEFT_PAREN, "(", None),
                (TT.IDENTIFIER, "n", None),
                (TT.LESS, "<", None),
                (TT.NUMBER, "2", 2.0),
                (TT.RIGHT_PAREN, ")", None),
                (TT.RETURN, "return", None),
                (TT.IDENTIFIER, "n", None),
                (TT.SEMICOLON, ";", None),
                (TT.RETURN, "return", None),
                (TT.IDENTIFIER, "fib", None),
                (TT.LEFT_PAREN, "(", None),
                (TT.IDENTIFIER, "n", None),
                (TT.MINUS, "-", None),
                (TT.NUMBER, "1", 1.0),
                (TT.RIGHT_PAREN, ")", None),
                (TT.PLUS, "+", None),
                (TT.IDENTIFIER, "fib", None),
                (TT.LEFT_PAREN, "(", None),
                (TT.IDENTIFIER, "n", None),
                (TT.MINUS, "-", None),
                (TT.NUMBER, "2", 2.0),
                (TT.RIGHT_PAREN, ")", None),
                (TT.SEMICOLON, ";", None),
                (TT.RIGHT_BRACE, "}", None),
            ],
        ),
        (
            """\
            // greet the user
            var name = "Lox";
            print "Hello, " + name;
            """,
            [
                (TT.VAR, "var", None),
                (TT.IDENTIFIER, "name", None),
                (TT.EQUAL, "=", None),
                (TT.STRING, '"Lox"', "Lox"),
                (TT.SEMICOLON, ";", None),
                (TT.PRINT, "print", None),
                (TT.STRING, '"Hello, "', "Hello, "),
                (TT.PLUS, "+", None),
                (TT.IDENTIFIER, "name", None),
                (TT.SEMICOLON, ";", None),
            ],
        ),
    ],
)
def test_full_programs(scan, source, expected):
    assert scan(source) == expected + [EOF]


@pytest.mark.parametrize(
    "source, expected",
    [
        # single line: column advances with each character (1-based)
        ("1 + 2", [("1", 1, 1), ("+", 1, 3), ("2", 1, 5)]),
        # a newline advances the line and resets the column to 1
        ("a\nbb\nc", [("a", 1, 1), ("bb", 2, 1), ("c", 3, 1)]),
        ("1\n+ 22", [("1", 1, 1), ("+", 2, 1), ("22", 2, 3)]),
        # leading indentation is reflected in the column
        ("if\n  x", [("if", 1, 1), ("x", 2, 3)]),
    ],
)
def test_token_line_and_column(source, expected):
    tokens = Scanner(source).scan()
    positions = [
        (token.lexeme, token.line_num, token.col_num)
        for token in tokens
        if token.type != TT.EOF
    ]
    assert positions == expected


@pytest.mark.parametrize(
    "source, position",
    [
        ("abc", (1, 4)),
        ("a\nbb\nc", (3, 2)),
        ("", (1, 1)),
    ],
)
def test_eof_position(source, position):
    eof = Scanner(source).scan()[-1]
    assert eof.type == TT.EOF
    assert (eof.line_num, eof.col_num) == position


@pytest.mark.parametrize(
    "source, char, position",
    [
        ("@", "@", (1, 1)),
        ("1 + #", "#", (1, 5)),
        ("foo\n  ^", "^", (2, 3)),
    ],
)
def test_unexpected_character(scan_errors, error_position, source, char, position):
    (error,) = scan_errors(source)
    assert str(error) == f"Unexpected character: '{char}'"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        ('"abc', (1, 1)),
        ('foo\n"bar', (2, 1)),
    ],
)
def test_unterminated_string(scan_errors, error_position, source, position):
    (error,) = scan_errors(source)
    assert str(error) == "Unterminated string"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, position",
    [
        ("/*", (1, 1)),
        ("/* never closed", (1, 1)),
        ("1\n/* dangling", (2, 1)),
        # an inner /* opens no level, so this stays unterminated
        ("/* outer /* inner", (1, 1)),
    ],
)
def test_unterminated_block_comment(scan_errors, error_position, source, position):
    (error,) = scan_errors(source)
    assert str(error) == "Unterminated comment"
    assert error_position(error) == position


@pytest.mark.parametrize(
    "source, expected",
    [
        # two bad characters: scanning continues past the first
        (
            "@#",
            [
                ("Unexpected character: '@'", (1, 1)),
                ("Unexpected character: '#'", (1, 2)),
            ],
        ),
        # errors on different lines are reported in source order
        (
            "@\n#",
            [
                ("Unexpected character: '@'", (1, 1)),
                ("Unexpected character: '#'", (2, 1)),
            ],
        ),
        # different error kinds coexist; the unterminated string, which
        # runs to end-of-input, is necessarily the last one reported
        (
            '@ "abc',
            [
                ("Unexpected character: '@'", (1, 1)),
                ("Unterminated string", (1, 3)),
            ],
        ),
    ],
)
def test_multiple_errors(scan_errors, error_position, source, expected):
    errors = scan_errors(source)
    assert [(str(e), error_position(e)) for e in errors] == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the rejected character yields no token; the ones around it survive
        (
            "var @ = 1;",
            [
                (TT.VAR, "var", None),
                (TT.EQUAL, "=", None),
                (TT.NUMBER, "1", 1.0),
                (TT.SEMICOLON, ";", None),
            ],
        ),
        # an unterminated string runs to end-of-input, so it yields nothing
        (
            'var s = "abc',
            [
                (TT.VAR, "var", None),
                (TT.IDENTIFIER, "s", None),
                (TT.EQUAL, "=", None),
            ],
        ),
        # ... and it consumes the rest of the input looking for its closing
        # quote, so nothing after the opening one survives, lines included
        (
            'var s = "abc\nprint 1;',
            [
                (TT.VAR, "var", None),
                (TT.IDENTIFIER, "s", None),
                (TT.EQUAL, "=", None),
            ],
        ),
        # the same for an unterminated block comment
        ("1 /* open", [(TT.NUMBER, "1", 1.0)]),
        # several errors in one source are all passed over
        ("@ 1 #", [(TT.NUMBER, "1", 1.0)]),
    ],
)
def test_ignore_errors_keeps_the_tokens_around_them(source, expected):
    """With `ignore_errors`, a broken source scans to whatever it did produce.

    The REPL's highlighter re-scans on every keystroke, where most inputs are
    half-typed; it needs the tokens either side of the incomplete part rather
    than an exception.
    """
    tokens = Scanner(source, ignore_errors=True).scan()
    assert [(t.type, t.lexeme, t.literal) for t in tokens] == expected + [EOF]


@pytest.mark.parametrize(
    "source",
    [
        "",
        "1 + 2;",
        'print "ok";',
        "// just a comment",
    ],
)
def test_ignore_errors_leaves_clean_sources_alone(scan, source):
    """A source with no errors scans identically either way."""
    tokens = Scanner(source, ignore_errors=True).scan()
    assert [(t.type, t.lexeme, t.literal) for t in tokens] == scan(source)


@pytest.mark.parametrize(
    "source, expected",
    [
        # the error swallows no position: what follows is still placed
        ("@\nvar x;", [(2, 1), (2, 5), (2, 6), (2, 7)]),
        ("1 @ 2;", [(1, 1), (1, 5), (1, 6), (1, 7)]),
    ],
)
def test_ignore_errors_still_positions_tokens(source, expected):
    tokens = Scanner(source, ignore_errors=True).scan()
    assert [(t.line_num, t.col_num) for t in tokens] == expected


@pytest.mark.parametrize(
    "source",
    [
        "var @ = 1;",
        'var s = "abc',
        "1 /* open",
    ],
)
def test_errors_are_raised_without_the_flag(scan_errors, source):
    """The flag is opt-in: the default stays fail-loud."""
    assert scan_errors(source)
