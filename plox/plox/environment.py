from plox.common import Token, InterpreterError, LoxValue


class Environment:
    def __init__(self, *, parent: "Environment | None" = None) -> None:
        self._vars: dict[str, LoxValue] = {}
        self._parent = parent

    def define(self, name: str, value: LoxValue) -> None:
        self._vars[name] = value

    def get(self, token: Token) -> LoxValue:
        name = token.lexeme
        if name in self._vars:
            return self._vars[name]

        if self._parent is not None:
            return self._parent.get(token)

        raise InterpreterError(f"Undefined variable: {name}", token)

    def assign(self, token: Token, value: LoxValue) -> None:
        name = token.lexeme
        if name in self._vars:
            self._vars[name] = value
            return

        if self._parent is not None:
            self._parent.assign(token, value)
            return

        raise InterpreterError(f"Undefined variable: {name}", token)
