#!/usr/bin/env python3
"""
Gate: ensure generated .ipp fragments do not end while a Qt macro invocation
(such as QStringLiteral(...)) is still open.

MSVC reports this as C1057 / unexpected EOF in macro expansion. It happens when
an implementation fragment is split inside a macro argument list even if the next
.ipp file continues the original text.
"""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
MACROS = (
    "QStringLiteral",
    "QByteArrayLiteral",
    "QLatin1String",
    "QLatin1StringView",
    "QT_TRANSLATE_NOOP",
    "QT_TR_NOOP",
)


def skip_string(text: str, i: int) -> int:
    # Raw string: R"delim(... )delim"
    if i > 0 and text[i - 1] == "R":
        paren = text.find("(", i + 1)
        if paren != -1 and paren - i <= 32:
            delim = text[i + 1:paren]
            end_token = ")" + delim + '"'
            end = text.find(end_token, paren + 1)
            return len(text) if end == -1 else end + len(end_token)
    i += 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == '"':
            return i + 1
        i += 1
    return len(text)


def skip_char(text: str, i: int) -> int:
    i += 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == "'":
            return i + 1
        i += 1
    return len(text)


def skip_ws(text: str, i: int) -> int:
    while i < len(text) and text[i].isspace():
        i += 1
    return i


def scan_macro_invocation(text: str, start: int) -> tuple[bool, int]:
    """Return (closed, end_index_after_invocation_or_eof)."""
    i = start
    depth = 0
    while i < len(text):
        if text.startswith("//", i):
            nl = text.find("\n", i + 2)
            i = len(text) if nl == -1 else nl + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end == -1 else end + 2
            continue
        ch = text[i]
        if ch == '"':
            if i > 0 and text[i - 1] == "R":
                paren = text.find("(", i + 1)
                if paren != -1 and paren - i <= 32:
                    delim = text[i + 1:paren]
                    end_token = ")" + delim + '"'
                    end = text.find(end_token, paren + 1)
                    if end == -1:
                        issues.append(("raw-string:" + delim, text.count("\n", 0, i - 1) + 1))
                        i = len(text)
                        continue
                    i = end + len(end_token)
                    continue
            i = skip_string(text, i)
            continue
        if ch == "'":
            i = skip_char(text, i)
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return True, i + 1
        i += 1
    return False, len(text)


def unsafe_macro_boundaries(path: Path) -> list[tuple[str, int]]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    issues: list[tuple[str, int]] = []
    i = 0
    while i < len(text):
        if text.startswith("//", i):
            nl = text.find("\n", i + 2)
            i = len(text) if nl == -1 else nl + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end == -1 else end + 2
            continue
        ch = text[i]
        if ch == '"':
            if i > 0 and text[i - 1] == "R":
                paren = text.find("(", i + 1)
                if paren != -1 and paren - i <= 32:
                    delim = text[i + 1:paren]
                    end_token = ")" + delim + '"'
                    end = text.find(end_token, paren + 1)
                    if end == -1:
                        issues.append(("raw-string:" + delim, text.count("\n", 0, i - 1) + 1))
                        i = len(text)
                        continue
                    i = end + len(end_token)
                    continue
            i = skip_string(text, i)
            continue
        if ch == "'":
            i = skip_char(text, i)
            continue
        matched = None
        for macro in MACROS:
            if text.startswith(macro, i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
                j = skip_ws(text, i + len(macro))
                if j < len(text) and text[j] == "(":
                    matched = (macro, j)
                    break
        if matched:
            macro, paren = matched
            closed, end = scan_macro_invocation(text, paren)
            if not closed:
                issues.append((macro, text.count("\n", 0, i) + 1))
                i = len(text)
            else:
                i = end
            continue
        i += 1
    return issues


def main() -> int:
    failures = []
    for path in sorted(SRC.rglob("*.ipp")):
        for macro, line in unsafe_macro_boundaries(path):
            failures.append((path.relative_to(ROOT), macro, line))
    if failures:
        print("[ipp_macro_boundaries_gate] ERROR: .ipp fragment ends inside a Qt macro invocation or raw string literal:")
        for rel, macro, line in failures:
            if macro.startswith("raw-string:"):
                delim = macro.split(":", 1)[1]
                print(f"  - {rel}:{line}: unclosed raw string literal R\"{delim}(... ){delim}\"")
            else:
                print(f"  - {rel}:{line}: unclosed {macro}(...)")
        return 1
    print("[ipp_macro_boundaries_gate] OK: .ipp fragment boundaries do not split Qt macro invocations or raw string literals")
    return 0


if __name__ == "__main__":
    sys.exit(main())
