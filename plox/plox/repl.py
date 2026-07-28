import sys
import dataclasses
from collections.abc import Generator
from contextlib import contextmanager
from pathlib import Path
from typing import Any, get_type_hints

from prompt_toolkit import PromptSession, print_formatted_text
from prompt_toolkit.formatted_text import HTML, FormattedText
from prompt_toolkit.history import FileHistory
from prompt_toolkit.key_binding import KeyBindings

from plox.ast_printer import AstPrinter
from plox.common import to_str
from plox.errors import InterpreterError, LoxError
from plox.interpreter import Interpreter
from plox.lexer import LoxLexer
from plox.parser import Parser
from plox.resolver import Resolver
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
BORDER_CHAR = "-"
BORDER_LEN = 30
PRINTED_ERROR_CAP = 5
PRINTED_STACK_CAP = 5


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


@dataclasses.dataclass
class _RunConfig:
    tokens: bool = False
    ast: bool = False


@contextmanager
def _padded_borders(
    name: str,
    *,
    enabled: bool = True,
) -> Generator[None]:
    def _print_border(label: str) -> None:
        if enabled:
            text = f" {name} {label} ".upper()
            pre_len = max(0, (BORDER_LEN - len(text)) // 2)
            post_len = max(0, BORDER_LEN - len(text) - pre_len)
            print(f"{BORDER_CHAR * pre_len}{text}{BORDER_CHAR * post_len}")

    _print_border("start")
    try:
        yield
    finally:
        _print_border("end")


def _run_code(
    source: str,
    interpreter: Interpreter,
    config: _RunConfig,
) -> None:
    metadata_shown = False
    tokens = Scanner(source).scan()

    if config.tokens:
        metadata_shown = True
        with _padded_borders("tokens"):
            for i, token in enumerate(tokens):
                print(f"{i + 1:04}  {token}")
        print()

    program = Parser(tokens).parse()

    if config.ast:
        metadata_shown = True
        with _padded_borders("ast"):
            for i, statement in enumerate(program.statements):
                s_expr = AstPrinter().print(statement)
                print(f"{i + 1:04}  {s_expr}")
        print()

    Resolver().resolve(program)

    with _padded_borders("output", enabled=metadata_shown):
        interpreter.interpret(program)


def _print_error(e: LoxError, source: str) -> None:
    line_info = tuple(e.get_line_info())
    source_lines = source.split("\n")
    stack_lines: list[str] = []

    if len(line_info) > PRINTED_STACK_CAP:
        skipped = len(line_info) - PRINTED_STACK_CAP
        suffix = "s" if skipped != 1 else ""
        stack_lines.append(f"... ({skipped} stack frame{suffix} skipped)\n")
        line_info = line_info[-PRINTED_STACK_CAP:]

    for line_num, col_num in line_info:
        # line / col position is one-based
        source_line = source_lines[line_num - 1]
        prefix = source_line[: col_num - 1]  # part before the error
        # this is to print possible tabs from the source as they are
        padding = "".join(c if c == "\t" else " " for c in prefix)

        pos = f"[{line_num}:{col_num}]"
        source_line_with_pos = f"{pos:<8} | {source_line}"
        shift = len(source_line_with_pos) - len(source_line)
        caret_line = f"{' ' * (shift - 3)} | {padding}^"
        stack_lines.extend((source_line_with_pos, caret_line))

    error_msg = f"[{type(e).__name__}] {e}"
    underline = "=" * len(error_msg)
    stack_info = "\n".join(stack_lines)
    error = "\n".join((error_msg, underline, stack_info))
    _print_formatted_error_text(error)


def _print_errors(
    e: LoxError | ExceptionGroup[LoxError],
    source: str,
    num_errors: int = 0,
    first_call: bool = True,
) -> int:
    if isinstance(e, ExceptionGroup):
        for nested_e in e.exceptions:
            num_errors = _print_errors(nested_e, source, num_errors, first_call=False)
    else:
        if num_errors < PRINTED_ERROR_CAP:
            _print_error(e, source)
        num_errors += 1

    if first_call and num_errors > PRINTED_ERROR_CAP:
        skipped = num_errors - PRINTED_ERROR_CAP
        suffix = "s" if skipped != 1 else ""
        _print_formatted_error_text(f"... ({skipped} error{suffix} skipped)")

    return num_errors


def _run_file(path: str) -> int:
    try:
        source = Path(path).read_text(encoding="utf-8")
    except OSError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 66  # EX_NOINPUT

    exit_code = 0  # EX_OK
    try:
        _run_code(
            source=source,
            interpreter=Interpreter(),  # fresh interpreter
            config=_RunConfig(),  # default config
        )
    except* InterpreterError as eg:
        _print_errors(eg, source)
        exit_code = 70  # EX_SOFTWARE (runtime error)
    except* LoxError as eg:
        _print_errors(eg, source)
        exit_code = 65  # EX_DATAERR (scan/parse error)

    return exit_code


def _handle_command(
    command: str,
    cfg: _RunConfig,
    interpreter: Interpreter,
) -> None:
    fields = get_type_hints(type(cfg))
    switches = [f for f in fields if fields[f] is bool]
    settings = {f: type_ for f, type_ in fields.items() if f not in switches}

    print_config = True

    def _error(msg: str) -> None:
        _print_formatted_error_text(msg)
        nonlocal print_config
        print_config = False

    match command.split():
        case [switch] if switch in switches:
            # flip the switch
            setattr(cfg, switch, not getattr(cfg, switch))
        case [switch, "on" | "off" as toggle] if switch in switches:
            # set the switch
            setattr(cfg, switch, toggle == "on")
        case [setting] if setting in settings:
            # flip the setting (error)
            _error(f"usage: {setting} <value>")
        case [setting, value] if setting in settings:
            # set the setting value
            try:
                type_ = settings[setting]
                setattr(cfg, setting, type_(value))
            except Exception:
                _error(f'can\'t set {setting} to "{value}"')
        case ["cfg" | "config"]:
            # print the current config
            pass
        case ["env" | "globals"]:
            # print the interpreter's globals
            for name, val in interpreter.globals.items():
                s = f'"{val}"' if isinstance(val, str) else to_str(val)
                print(f"{name} = {s}")
            print_config = False
        case ["help" | "h"]:
            # print the recognized commands
            rows = [
                (":env | :globals", "print the global variables"),
                (":cfg | :config", "print the current config"),
                *((f":{s} [on | off]", f"toggle/set {s} output") for s in switches),
                *((f":{s} <value>", f"set {s}") for s in settings),
                (":help | :h", "print this message"),
                (":exit | :quit | :q", "quit the REPL"),
            ]
            width = max(len(usage) for usage, _ in rows)
            for usage, description in rows:
                print(f"{usage:<{width}}    {description}")
            print_config = False
        case _:
            _error(f'unrecognized command: "{command}"')

    if print_config:
        for key, value in dataclasses.asdict(cfg).items():
            type_ = fields[key].__name__
            if key in switches:
                print(f"{key} = {('on' if value else 'off')} [{type_}]")
            else:
                print(f"{key} = {value!r} [{type_}]")


def _run_repl() -> int:
    prompt = f"<b><{PROMPT_COLOR}>{PROMPT_TEXT}</{PROMPT_COLOR}></b>"
    multiline_hint = HTML(f"{prompt}<{HINT_COLOR}>{MULTILINE_HINT}</{HINT_COLOR}>")
    prompt_line = HTML(prompt)

    lexer = LoxLexer()
    history = _FilteredHistory(str(HISTORY_PATH))
    single_line = PromptSession[str](
        lexer=lexer,
        history=history,
    )
    multi_line = PromptSession[str](
        lexer=lexer,
        history=history,
        multiline=True,
        key_bindings=_make_multiline_bindings(),
        prompt_continuation=_continuation_prompt,
    )

    # the interpreter instance and REPL config
    # persist for the whole session duration
    interpreter = Interpreter()
    cfg = _RunConfig()

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
                _handle_command(command, cfg, interpreter)
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
                config=cfg,
            )
        except* LoxError as eg:
            _print_errors(eg, text)

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
