#!/usr/bin/env python
"""Lexical discovery scanner for the ecc_py binding census.

A small lexer (a state machine over code / line comment / block comment /
string literal / char literal with paren-brace-bracket depth tracking — no
cross-line regex) that finds every module-level ``m.def(`` in the register
files, including multiline and commented-out statements, extracts the
``py::arg("name") = default`` entries at paren depth 1, and resolves whether
a binding is ``active`` or ``disabled`` (commented statement, or its
enclosing ``register_*`` function is never called from
``python_moodule.cc``).
"""
import re
from dataclasses import dataclass, field
from pathlib import Path

PYTHON_INTERFACE_DIR = Path("src/interface/python")
MODULE_CC = PYTHON_INTERFACE_DIR / "python_moodule.cc"


@dataclass(frozen=True)
class Span:
    kind: str  # "code" | "line_comment" | "block_comment" | "string" | "char"
    start: int
    end: int  # exclusive
    start_line: int  # 1-based line of span start


@dataclass(frozen=True)
class DiscoveredParam:
    name: str
    default: str | None

    def to_json(self) -> dict:
        return {"name": self.name, "default": self.default}


@dataclass
class DiscoveredBinding:
    module: str
    file: str
    line: int
    py_name: str
    cpp_target: str
    register_function: str | None
    commented: bool
    status_in_source: str  # "active" | "disabled"
    params: list[DiscoveredParam] = field(default_factory=list)
    raw: str = ""

    def to_json(self) -> dict:
        return {
            "module": self.module,
            "file": self.file,
            "line": self.line,
            "py_name": self.py_name,
            "cpp_target": self.cpp_target,
            "register_function": self.register_function,
            "commented": self.commented,
            "status_in_source": self.status_in_source,
            "params": [p.to_json() for p in self.params],
        }


def lex_spans(text: str) -> list[Span]:
    """Split C++ source into lexical spans with a state machine.

    States: code, line comment, block comment, string literal (with escapes),
    char literal (with escapes). Newlines inside block comments and literals
    are tracked so every span carries an accurate start line.
    """
    spans: list[Span] = []
    i = 0
    n = len(text)
    line = 1
    state = "code"
    span_start = 0
    span_line = 1

    def emit(kind: str, end: int) -> None:
        if end > span_start:
            spans.append(Span(kind, span_start, end, span_line))

    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                emit("code", i)
                state, span_start, span_line = "line_comment", i, line
                i += 2
                continue
            if c == "/" and nxt == "*":
                emit("code", i)
                state, span_start, span_line = "block_comment", i, line
                i += 2
                continue
            if c == '"':
                emit("code", i)
                state, span_start, span_line = "string", i, line
                i += 1
                continue
            if c == "'":
                emit("code", i)
                state, span_start, span_line = "char", i, line
                i += 1
                continue
            if c == "\n":
                line += 1
            i += 1
        elif state == "line_comment":
            if c == "\n":
                emit("line_comment", i)
                state, span_start, span_line = "code", i, line
                line += 1
            i += 1
        elif state == "block_comment":
            if c == "*" and nxt == "/":
                emit("block_comment", i + 2)
                state, span_start, span_line = "code", i + 2, line
                i += 2
                continue
            if c == "\n":
                line += 1
            i += 1
        else:  # string or char literal
            quote = '"' if state == "string" else "'"
            if c == "\\":
                i += 2
                continue
            if c == quote:
                emit(state, i + 1)
                state, span_start, span_line = "code", i + 1, line
                i += 1
                continue
            if c == "\n":
                line += 1
            i += 1
    emit(state, n)
    return spans


_IDENT_CHARS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def _span_index_at(spans: list[Span], pos: int) -> int:
    """Binary search: index of the span containing ``pos``."""
    lo, hi = 0, len(spans) - 1
    while lo < hi:
        mid = (lo + hi) // 2
        if pos >= spans[mid].end:
            lo = mid + 1
        else:
            hi = mid
    return lo


def _find_m_def_calls(text: str, spans: list[Span]) -> list[tuple[int, bool]]:
    """Locate every ``m.def(`` occurrence in code and comment spans.

    Returns (position of the ``m``, commented) pairs. Class-member ``.def(``
    chained on a ``py::class_<...>(m, ...)`` object is rejected because the
    character before ``.def`` is not the module identifier ``m``.
    """
    hits: list[tuple[int, bool]] = []
    for span in spans:
        if span.kind not in ("code", "line_comment", "block_comment"):
            continue
        seg = text[span.start : span.end]
        j = 0
        while True:
            k = seg.find("m.def", j)
            if k == -1:
                break
            j = k + 5
            abs_pos = span.start + k
            before = text[abs_pos - 1] if abs_pos > 0 else ""
            if before in _IDENT_CHARS or before == ".":
                continue
            rest = seg[k + 5 :]
            if re.match(r"\s*\(", rest):
                hits.append((abs_pos, span.kind != "code"))
    return hits


def _skip_span(spans: list[Span], si: int, i: int) -> tuple[int, int]:
    """Advance (span index, position) past the current position."""
    while si + 1 < len(spans) and i >= spans[si + 1].start:
        si += 1
    return si, i


def _matching_paren(text: str, spans: list[Span], open_paren: int) -> int:
    """Return the index just past the ')' matching text[open_paren]."""
    depth = 0
    si = _span_index_at(spans, open_paren)
    i = open_paren
    while i < len(text):
        span = spans[si]
        if span.kind != "code":
            i = span.end
            si, i = _skip_span(spans, si, i)
            continue
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError(f"unbalanced parens from offset {open_paren}")


def _split_top_level_args(text: str, spans: list[Span], start: int, end: int) -> list[tuple[int, int]]:
    """Split the call argument range (start=index of '(', end=past ')') into
    top-level argument ranges.

    Commas nested inside parens, braces (lambda bodies), brackets, string or
    char literals, comments, and template angle brackets do not split. Angle
    brackets are tracked heuristically (a ``>`` only closes when angle depth
    is positive and it is not part of ``->``); sufficient for the default
    expressions used in these register files, e.g.
    ``std::map<std::string, std::string>{}``.
    """
    args: list[tuple[int, int]] = []
    paren = brace = bracket = angle = 0
    arg_start = start + 1
    si = _span_index_at(spans, start)
    i = start + 1
    limit = end - 1
    while i < limit:
        span = spans[si]
        if span.kind != "code":
            i = span.end
            si, i = _skip_span(spans, si, i)
            continue
        c = text[i]
        if c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "{":
            brace += 1
        elif c == "}":
            brace -= 1
        elif c == "[":
            bracket += 1
        elif c == "]":
            bracket -= 1
        elif c == "<":
            angle += 1
        elif c == ">":
            if angle > 0 and text[i - 1] != "-":
                angle -= 1
        elif c == "," and paren == 0 and brace == 0 and bracket == 0 and angle == 0:
            args.append((arg_start, i))
            arg_start = i + 1
        i += 1
    if text[arg_start:limit].strip():
        args.append((arg_start, limit))
    return args


def _decode_string_literal(literal: str) -> str:
    """Decode the contents of a double-quoted C++ string literal."""
    body = literal[1:-1]
    out: list[str] = []
    i = 0
    escapes = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'"}
    while i < len(body):
        c = body[i]
        if c == "\\" and i + 1 < len(body):
            out.append(escapes.get(body[i + 1], body[i + 1]))
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def _normalize_default(text: str) -> str:
    """Whitespace-normalize a captured default expression."""
    return " ".join(text.split())


def _string_literal_at(text: str, spans: list[Span], start: int, end: int) -> str | None:
    """If the range holds exactly one string literal (plus whitespace), decode it."""
    si = _span_index_at(spans, start)
    while si < len(spans) and spans[si].start < end:
        span = spans[si]
        if span.kind == "string" and span.start >= start and span.end <= end:
            if text[start : span.start].strip() or text[span.end : end].strip():
                return None
            return _decode_string_literal(text[span.start : span.end])
        si += 1
    return None


def _parse_py_arg(text: str, spans: list[Span], start: int, end: int) -> DiscoveredParam | None:
    """Parse one top-level argument range as ``py::arg("name") = default``."""
    stripped = text[start:end]
    match = re.match(r"\s*py::arg\s*\(", stripped)
    if not match:
        return None
    open_paren = start + match.end() - 1
    close_paren = _matching_paren(text, spans, open_paren)
    name = _string_literal_at(text, spans, open_paren + 1, close_paren - 1)
    if name is None:
        raise ValueError(f"py::arg without a plain string name at offset {start}")
    rest = text[close_paren:end].strip()
    default = None
    if rest.startswith("="):
        default = _normalize_default(rest[1:])
    elif rest:
        raise ValueError(f"unexpected trailing tokens after py::arg at offset {start}: {rest!r}")
    return DiscoveredParam(name=name, default=default)


def _parse_m_def_core(
    text: str, spans: list[Span], m_pos: int
) -> tuple[str, str, list[DiscoveredParam], str]:
    """Parse one m.def call whose ``m`` is at m_pos in lexed code text."""
    open_paren = text.index("(", m_pos)
    end = _matching_paren(text, spans, open_paren)
    args = _split_top_level_args(text, spans, open_paren, end)
    if len(args) < 2:
        raise ValueError(f"m.def with fewer than two arguments at offset {m_pos}")
    py_name = _string_literal_at(text, spans, args[0][0], args[0][1])
    if py_name is None:
        raise ValueError(f"m.def first argument is not a string literal at offset {m_pos}")
    target_text = text[args[1][0] : args[1][1]].strip()
    if target_text.startswith("["):
        cpp_target = "<lambda>"
    else:
        cpp_target = target_text.lstrip("&").split()[0].rstrip(",")
    params: list[DiscoveredParam] = []
    for arg_start, arg_end in args[2:]:
        param = _parse_py_arg(text, spans, arg_start, arg_end)
        if param is not None:
            params.append(param)
    return py_name, cpp_target, params, text[m_pos:end]


def _parse_m_def_statement(
    text: str, spans: list[Span], m_pos: int, commented: bool
) -> tuple[str, str, int, list[DiscoveredParam], str]:
    """Parse one m.def statement starting at the ``m`` of ``m.def``.

    Returns (py_name, cpp_target, start_line, params, raw_statement). For
    commented-out statements the enclosing comment's content is stripped of
    its comment markers and re-lexed as code, so strings and nesting inside
    it are handled by the same machinery.
    """
    start_line = text.count("\n", 0, m_pos) + 1
    if commented:
        span = spans[_span_index_at(spans, m_pos)]
        sub = text[m_pos : span.end]
        if span.kind == "block_comment":
            lines = sub.split("\n")
            lines = [lines[0]] + [re.sub(r"^\s*\*", "", line) for line in lines[1:]]
            sub = re.sub(r"\*/\s*$", "", "\n".join(lines))
        py_name, cpp_target, params, raw = _parse_m_def_core(sub, lex_spans(sub), 0)
        return py_name, cpp_target, start_line, params, raw
    py_name, cpp_target, params, raw = _parse_m_def_core(text, spans, m_pos)
    return py_name, cpp_target, start_line, params, raw


def _find_register_function_bodies(text: str, spans: list[Span]) -> list[tuple[str, int, int]]:
    """Return (name, body_start, body_end) for every ``register_*`` function
    definition in the file."""
    code_only = list(text)
    for span in spans:
        if span.kind != "code":
            for pos in range(span.start, span.end):
                if code_only[pos] != "\n":
                    code_only[pos] = " "
    code_text = "".join(code_only)
    bodies = []
    for match in re.finditer(r"\bregister_\w+\s*\([^)]*\)\s*\{", code_text):
        name = match.group(0).split("(")[0].strip()
        open_brace = match.end() - 1
        depth = 0
        i = open_brace
        while i < len(code_text):
            if code_text[i] == "{":
                depth += 1
            elif code_text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        bodies.append((name, open_brace, i + 1))
    return bodies


def parse_called_register_functions(text: str) -> set[str]:
    """Parse python_moodule.cc for the set of ``register_*`` calls that are
    actually made (commented-out calls are excluded)."""
    spans = lex_spans(text)
    called: set[str] = set()
    for span in spans:
        if span.kind != "code":
            continue
        for match in re.finditer(r"\b(register_\w+)\s*\(", text[span.start : span.end]):
            called.add(match.group(1))
    return called


def discover_bindings_in_text(
    text: str, module: str, file: str, called_registers: set[str]
) -> list[DiscoveredBinding]:
    """Discover every module-level m.def binding in one register file."""
    spans = lex_spans(text)
    bodies = _find_register_function_bodies(text, spans)
    bindings: list[DiscoveredBinding] = []
    for m_pos, commented in _find_m_def_calls(text, spans):
        py_name, cpp_target, line, params, raw = _parse_m_def_statement(text, spans, m_pos, commented)
        register_function = next(
            (name for name, body_start, body_end in bodies if body_start <= m_pos < body_end), None
        )
        if commented or register_function is None or register_function not in called_registers:
            status = "disabled"
        else:
            status = "active"
        bindings.append(
            DiscoveredBinding(
                module=module,
                file=file,
                line=line,
                py_name=py_name,
                cpp_target=cpp_target,
                register_function=register_function,
                commented=commented,
                status_in_source=status,
                params=params,
                raw=raw,
            )
        )
    return bindings


def _register_files(repo_root: Path) -> list[Path]:
    base = repo_root / PYTHON_INTERFACE_DIR
    files = sorted(base.glob("py_*/py_register_*.h")) + sorted(base.glob("py_*/py_register_*.cpp"))
    return files


def discover(repo_root: Path) -> dict:
    """Run discovery over the whole python interface tree."""
    module_cc = repo_root / MODULE_CC
    called = parse_called_register_functions(module_cc.read_text())
    bindings: list[DiscoveredBinding] = []
    for path in _register_files(repo_root):
        rel = path.relative_to(repo_root).as_posix()
        module = path.parent.name
        bindings.extend(discover_bindings_in_text(path.read_text(), module, rel, called))
    return {
        "called_register_functions": sorted(called),
        "bindings": bindings,
    }


def discovery_to_json(discovery: dict) -> dict:
    return {
        "called_register_functions": discovery["called_register_functions"],
        "bindings": [b.to_json() for b in discovery["bindings"]],
    }
