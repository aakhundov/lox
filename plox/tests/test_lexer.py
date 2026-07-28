import pytest
from prompt_toolkit.document import Document

from plox.common import TokenType as TT
from plox.lexer import _DATA, _NAME, _PUNCTUATION, _STYLES, _SYNTAX, LoxLexer

# the styles are asserted by role, never by hex: a token's group is
# the contract, the color it happens to carry today is a theme choice


@pytest.fixture
def lex():
    """Return a helper lexing `source` into one [(style, text), ...] list per line.

    Fragments whose text is empty are dropped: the EOF token contributes a
    zero-width fragment to the last line, and a multi-line string contributes
    one to every empty line it spans. Neither renders, so neither belongs in an
    expectation -- `test_fragments_tile_the_line` is the one that sees them.
    """

    def _lex(source):
        document = Document(source, 0)
        get_line = LoxLexer().lex_document(document)
        return [
            # indexed, not unpacked: a fragment may carry a third
            # element, a mouse handler, which this lexer never emits
            [(f[0], f[1]) for f in get_line(i) if f[1]]
            for i in range(len(document.lines))
        ]

    return _lex


@pytest.fixture
def lex_raw():
    """Return a helper lexing `source` into per-line fragments, nothing dropped."""

    def _lex_raw(source):
        document = Document(source, 0)
        get_line = LoxLexer().lex_document(document)
        return document.lines, [get_line(i) for i in range(len(document.lines))]

    return _lex_raw


@pytest.mark.parametrize(
    "source",
    [
        "",
        "\n",
        "var a = 1;",
        "var a = 1;\n\n\n",
        "  \t  ",
        # multi-line strings: the one token whose span is split by hand
        'var s = "a\nb";',
        'var s = "a\nb\nc";',
        'var s = "a\n\nb";',
        'var s = "ab"\n+ "c";',
        'var s = "";',
        # comments contribute no token, so their text comes from the gaps
        "var a = 1; // trailing\nvar b = 2;",
        "var a = /* one\ntwo\nthree */ 2;",
        # incomplete input, as seen mid-keystroke
        'var s = "abc',
        'var s = "abc\ndef',
        "var a = /* open",
        "var @ = 1;",
        # non-token characters around the edges
        "\tvar\ta\t=\t1;",
        "var a = 1;\r\nvar b = 2;\r\n",
        "var é = 1;",
    ],
)
def test_fragments_tile_the_line(lex_raw, source):
    """Every line is reproduced exactly by concatenating its fragments.

    This is the whole contract with prompt_toolkit: the renderer paints what it
    is handed, so a lost or duplicated character is a corrupted prompt.
    """
    lines, per_line = lex_raw(source)
    assert ["".join(text for _, text in frags) for frags in per_line] == lines


@pytest.mark.parametrize(
    "source",
    [
        "var",
        "fun",
        "class",
        "if",
        "else",
        "for",
        "while",
        "return",
        "break",
        "continue",
        "and",
        "or",
        "print",
        "this",
        "super",
    ],
)
def test_keywords_are_syntax(lex, source):
    assert lex(source) == [[(_SYNTAX, source)]]


@pytest.mark.parametrize(
    "source",
    [
        "1",
        "1.5",
        "0",
        '"abc"',
        '""',
        # value keywords are literals, not syntax
        "true",
        "false",
        "nil",
    ],
)
def test_literals_are_data(lex, source):
    assert lex(source) == [[(_DATA, source)]]


@pytest.mark.parametrize(
    "source",
    [
        "(",
        ")",
        "{",
        "}",
        ",",
        ".",
        ";",
        "?",
        ":",
        "-",
        "+",
        "/",
        "*",
        "!",
        "!=",
        "=",
        "==",
        ">",
        ">=",
        "<",
        "<=",
    ],
)
def test_operators_and_separators_are_punctuation(lex, source):
    assert lex(source) == [[(_PUNCTUATION, source)]]


@pytest.mark.parametrize("source", ["a", "foo", "_x", "camelCase", "n2"])
def test_identifiers_keep_the_default_style(lex, source):
    assert lex(source) == [[(_NAME, source)]]


def test_every_token_type_has_a_style():
    """A new TokenType must be given a role; only EOF is deliberately absent.

    EOF's lexeme is empty, so it never reaches the screen -- every other type
    does, and one missing from the table would silently render unstyled.
    """
    assert {tt for tt in TT if tt not in _STYLES} == {TT.EOF}


def test_roles_are_visually_distinct():
    """Two roles sharing a color would make the highlighting a lie."""
    assert len({_SYNTAX, _DATA, _PUNCTUATION, _NAME}) == 4


@pytest.mark.parametrize(
    "source, expected",
    [
        # a line comment and the whitespace around tokens are not tokens, and
        # everything between two tokens arrives as a single fragment
        (
            "a // note",
            [[(_NAME, "a"), ("", " // note")]],
        ),
        (
            "  a  ",
            [[("", "  "), (_NAME, "a"), ("", "  ")]],
        ),
        # a block comment spans lines the same way, contributing no token
        (
            "a /* one\ntwo */ b",
            [
                [(_NAME, "a"), ("", " /* one")],
                [("", "two */ "), (_NAME, "b")],
            ],
        ),
    ],
)
def test_comments_and_whitespace_are_unstyled(lex, source, expected):
    assert lex(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the token's span is split across the lines it covers, quotes included
        (
            'var s = "a\nb";',
            [
                [
                    (_SYNTAX, "var"),
                    ("", " "),
                    (_NAME, "s"),
                    ("", " "),
                    (_PUNCTUATION, "="),
                    ("", " "),
                    (_DATA, '"a'),
                ],
                [(_DATA, 'b"'), (_PUNCTUATION, ";")],
            ],
        ),
        # an interior line belongs entirely to the string
        (
            'var s = "a\nb\nc";',
            [
                [
                    (_SYNTAX, "var"),
                    ("", " "),
                    (_NAME, "s"),
                    ("", " "),
                    (_PUNCTUATION, "="),
                    ("", " "),
                    (_DATA, '"a'),
                ],
                [(_DATA, "b")],
                [(_DATA, 'c"'), (_PUNCTUATION, ";")],
            ],
        ),
        # an empty interior line has nothing to style
        (
            'var s = "a\n\nb";',
            [
                [
                    (_SYNTAX, "var"),
                    ("", " "),
                    (_NAME, "s"),
                    ("", " "),
                    (_PUNCTUATION, "="),
                    ("", " "),
                    (_DATA, '"a'),
                ],
                [],
                [(_DATA, 'b"'), (_PUNCTUATION, ";")],
            ],
        ),
        # the string ends where the line does, and the next line starts fresh
        (
            'print "ab"\n+ "c";',
            [
                [(_SYNTAX, "print"), ("", " "), (_DATA, '"ab"')],
                [
                    (_PUNCTUATION, "+"),
                    ("", " "),
                    (_DATA, '"c"'),
                    (_PUNCTUATION, ";"),
                ],
            ],
        ),
    ],
)
def test_multi_line_strings_are_styled_on_every_line(lex, source, expected):
    assert lex(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # a full line, to pin the roles against each other in one place
        (
            'if (n <= 1) print "x", nil;',
            [
                [
                    (_SYNTAX, "if"),
                    ("", " "),
                    (_PUNCTUATION, "("),
                    (_NAME, "n"),
                    ("", " "),
                    (_PUNCTUATION, "<="),
                    ("", " "),
                    (_DATA, "1"),
                    (_PUNCTUATION, ")"),
                    ("", " "),
                    (_SYNTAX, "print"),
                    ("", " "),
                    (_DATA, '"x"'),
                    (_PUNCTUATION, ","),
                    ("", " "),
                    (_DATA, "nil"),
                    (_PUNCTUATION, ";"),
                ]
            ],
        ),
        # a keyword's lookalike is an identifier, not syntax
        (
            "variable = varied;",
            [
                [
                    (_NAME, "variable"),
                    ("", " "),
                    (_PUNCTUATION, "="),
                    ("", " "),
                    (_NAME, "varied"),
                    (_PUNCTUATION, ";"),
                ]
            ],
        ),
    ],
)
def test_whole_lines(lex, source, expected):
    assert lex(source) == expected


@pytest.mark.parametrize(
    "source, expected",
    [
        # the string is not a literal until it closes, so it stays unstyled
        (
            'var s = "abc',
            [
                [
                    (_SYNTAX, "var"),
                    ("", " "),
                    (_NAME, "s"),
                    ("", " "),
                    (_PUNCTUATION, "="),
                    ("", ' "abc'),
                ]
            ],
        ),
        # the same for a comment that has not been closed
        (
            "a /* open",
            [[(_NAME, "a"), ("", " /* open")]],
        ),
        # a character the scanner rejects styles nothing and drops nothing
        (
            "var @ = 1;",
            [
                [
                    (_SYNTAX, "var"),
                    ("", " @ "),
                    (_PUNCTUATION, "="),
                    ("", " "),
                    (_DATA, "1"),
                    (_PUNCTUATION, ";"),
                ]
            ],
        ),
    ],
)
def test_incomplete_input_is_lexed_not_rejected(lex, source, expected):
    """Every keystroke is lexed, including the half-typed states in between."""
    assert lex(source) == expected


@pytest.mark.parametrize("source", ["", "var a = 1;", "var s = 'a\nb';"])
def test_lines_past_the_end_are_empty(source):
    """The renderer may ask for a line that is no longer there."""
    document = Document(source, 0)
    get_line = LoxLexer().lex_document(document)

    assert get_line(len(document.lines)) == []
    assert get_line(len(document.lines) + 10) == []


def test_lexer_is_reusable_across_documents():
    """One instance serves the whole session, a new document on every keystroke."""
    lexer = LoxLexer()

    first = lexer.lex_document(Document("var a = 1;", 0))
    second = lexer.lex_document(Document('print "x";', 0))

    assert first(0)[0] == (_SYNTAX, "var")
    assert second(0)[0] == (_SYNTAX, "print")
    # the first getter still reports on the document it was made for
    assert first(0)[0] == (_SYNTAX, "var")
