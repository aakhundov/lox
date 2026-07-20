import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from prompt_toolkit import PromptSession, print_formatted_text
from prompt_toolkit.formatted_text import HTML, FormattedText
from prompt_toolkit.history import FileHistory
from prompt_toolkit.key_binding import KeyBindings

from plox.common import LoxError
from plox.ast_printer import AstPrinter
from plox.interpreter import Interpreter
from plox.parser import Parser
from plox.scanner import Scanner


PROMPT_TEXT = ">>> "
PROMPT_COLOR = "ansigreen"
CONTINUATION_TEXT = "... "
CONTINUATION_COLOR = "ansibrightblack"
ERROR_COLOR = "ansired"
MULTILINE_TRIGGER = ""
MULTILINE_HINT = "MULTI-LINE ENTRY [\\n\\n to submit]:"
HINT_COLOR = "ansibrightblack"
HISTORY_PATH = Path.home() / ".lox.history"


def _make_multiline_bindings() -> KeyBindings:
    kb = KeyBindings()

    @kb.add("enter")
    def _(event: Any) -> None:
        buf = event.current_buffer
        lines = buf.document.lines
        if len(lines) >= 2 and lines[-2].strip() == "" and lines[-1].strip() == "":
            # input ends with two blank lines: drop them, then submit
            buf.text = buf.text.rstrip("\n")
            buf.cursor_position = len(buf.text)
            buf.validate_and_handle()
        else:
            # just one blank line: keep editing
            buf.insert_text("\n")

    return kb


def _continuation_prompt(*_: Any) -> HTML:
    return HTML(f"<{CONTINUATION_COLOR}>{CONTINUATION_TEXT}</{CONTINUATION_COLOR}>")


def _print_formatted_error_text(error: str) -> None:
    formatted = FormattedText([(f"fg:{ERROR_COLOR}", error)])
    print_formatted_text(formatted, file=sys.stderr)


class _FilteredHistory(FileHistory):
    """FileHistory that never records commands."""

    def append_string(self, string: str) -> None:
        if string.lower().strip().startswith(":"):
            return
        super().append_string(string)


@dataclass
class _RunConfig:
    print_tokens: bool = False
    print_ast: bool = False


def _run_code(
    source: str,
    interpreter: Interpreter,
    config: _RunConfig,
) -> None:
    tokens = Scanner(source).scan()

    if config.print_tokens:
        for i, token in enumerate(tokens):
            print(f"{i + 1:04}  {token}")
        print()

    statements = Parser(tokens).parse()

    if config.print_ast:
        for i, statement in enumerate(statements):
            s_expr = AstPrinter().print(statement)
            print(f"{i + 1:04}  {s_expr}")
        print()

    interpreter.interpret(statements)


def _print_error(e: LoxError, source: str) -> None:
    line_num, col_num = e.get_line_info()
    msg = f"{e} [at {line_num}:{col_num}]:"

    # line / col position is one-based
    source_line = source.split("\n")[line_num - 1]
    prefix = source_line[: col_num - 1]  # part before the error
    # this is to print possible tabs from the source as they are
    padding = "".join(c if c == "\t" else " " for c in prefix)
    caret = padding + "^"

    error = f"{msg}\n{'=' * len(msg)}\n{source_line}\n{caret}"
    _print_formatted_error_text(error)


def _run_file(path: str) -> int:
    try:
        source = Path(path).read_text(encoding="utf-8")
    except OSError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 66  # EX_NOINPUT

    try:
        _run_code(
            source=source,
            interpreter=Interpreter(),  # fresh interpreter
            config=_RunConfig(),  # default config
        )
    except LoxError as e:
        _print_error(e, source)
        return 65  # EX_DATAERR

    return 0  # EX_OK


def _update_repl_config(cfg: _RunConfig, command: str) -> None:
    match command.split():
        case ["tokens", "on" | "off" as toggle]:
            cfg.print_tokens = toggle == "on"
        case ["ast", "on" | "off" as toggle]:
            cfg.print_ast = toggle == "on"
        case _:
            _print_formatted_error_text(f'unrecognized command: "{command}"')


def _run_repl() -> int:
    prompt = f"<b><{PROMPT_COLOR}>{PROMPT_TEXT}</{PROMPT_COLOR}></b>"
    multiline_hint = HTML(f"{prompt}<{HINT_COLOR}>{MULTILINE_HINT}</{HINT_COLOR}>")
    prompt_line = HTML(prompt)

    history = _FilteredHistory(str(HISTORY_PATH))
    single_line = PromptSession[str](history=history)
    multi_line = PromptSession[str](
        history=history,
        multiline=True,
        key_bindings=_make_multiline_bindings(),
        prompt_continuation=_continuation_prompt,
    )

    # the interpreter instance and REPL config
    # persist for the whole session duration
    interpreter = Interpreter()
    repl_cfg = _RunConfig()

    while True:
        try:
            text = single_line.prompt(prompt_line)
            if text == MULTILINE_TRIGGER:
                # annotate the prompt line above with a faint hint, then
                # start multi-line input on a fresh empty line below it
                print("\x1b[A", end="", flush=True)  # cursor up
                print_formatted_text(multiline_hint)
                text = multi_line.prompt(_continuation_prompt())
            elif (command := text.lower().strip()).startswith(":"):
                command = command[1:].strip()  # drop the ':'
                if command in ("exit", "quit", "q"):
                    break  # quit the REPL
                _update_repl_config(repl_cfg, command)
                continue
        except KeyboardInterrupt:
            # Ctrl-C: discard current line, keep going
            continue
        except EOFError:
            # Ctrl-D: exit
            break

        try:
            _run_code(
                source=text,
                interpreter=interpreter,
                config=repl_cfg,
            )
        except LoxError as e:
            _print_error(e, text)

    return 0  # EX_OK


def main() -> None:
    args = sys.argv[1:]
    if len(args) == 0:
        sys.exit(_run_repl())
    elif len(args) == 1:
        sys.exit(_run_file(args[0]))
    else:
        print("Usage: plox <path_to_file>", file=sys.stderr)
        sys.exit(64)  # EX_USAGE
