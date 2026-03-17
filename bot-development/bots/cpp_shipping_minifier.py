#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path


def is_word_char(ch: str) -> bool:
    return ch.isalnum() or ch == "_"


def needs_space(prev: str | None, curr: str) -> bool:
    if not prev:
        return False

    if is_word_char(prev) and is_word_char(curr):
        return True

    if (is_word_char(prev) and curr in {'"', "'"}) or (prev in {'"', "'"} and is_word_char(curr)):
        return True

    pair = prev + curr
    dangerous_pairs = {
        "++",
        "--",
        "&&",
        "||",
        "<<",
        ">>",
        "==",
        "!=",
        "<=",
        ">=",
        "+=",
        "-=",
        "*=",
        "/=",
        "%=",
        "^=",
        "&=",
        "|=",
        "->",
        "::",
        "/*",
        "//",
    }
    return pair in dangerous_pairs


def parse_raw_delimiter(src: str, i: int) -> tuple[str, int] | None:
    if i + 2 >= len(src):
        return None
    if src[i] != "R" or src[i + 1] != '"':
        return None

    j = i + 2
    delim_chars = []
    while j < len(src) and src[j] != "(":
        ch = src[j]
        if ch.isspace() or ch == "\\":
            return None
        delim_chars.append(ch)
        j += 1
    if j >= len(src) or src[j] != "(":
        return None

    return "".join(delim_chars), j + 1


def strip_comments(src: str) -> str:
    out: list[str] = []
    i = 0
    n = len(src)
    state = "normal"
    raw_delim = ""

    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""

        if state == "normal":
            raw = parse_raw_delimiter(src, i)
            if raw is not None:
                raw_delim, body_start = raw
                out.append(src[i:body_start])
                i = body_start
                state = "raw"
                continue

            if ch == "/" and nxt == "/":
                i += 2
                state = "line_comment"
                continue
            if ch == "/" and nxt == "*":
                i += 2
                state = "block_comment"
                continue

            out.append(ch)
            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            i += 1
            continue

        if state == "line_comment":
            if ch == "\n":
                out.append("\n")
                state = "normal"
            i += 1
            continue

        if state == "block_comment":
            if ch == "*" and nxt == "/":
                i += 2
                state = "normal"
            else:
                if ch == "\n":
                    out.append("\n")
                i += 1
            continue

        if state == "string":
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == '"':
                state = "normal"
            i += 1
            continue

        if state == "char":
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == "'":
                state = "normal"
            i += 1
            continue

        if state == "raw":
            end_seq = ")" + raw_delim + '"'
            if src.startswith(end_seq, i):
                out.append(end_seq)
                i += len(end_seq)
                state = "normal"
                continue
            out.append(ch)
            i += 1
            continue

    return "".join(out)


def minify_whitespace(src: str) -> str:
    out: list[str] = []
    i = 0
    n = len(src)
    state = "normal"
    raw_delim = ""
    pending_space = False
    pending_newline = False

    def flush(curr: str) -> None:
        nonlocal pending_space, pending_newline
        prev = out[-1] if out else None

        if pending_newline:
            if out and out[-1] != "\n":
                out.append("\n")
            pending_newline = False
            pending_space = False
            return

        if pending_space and needs_space(prev, curr):
            out.append(" ")
        pending_space = False

    while i < n:
        ch = src[i]

        if state == "normal":
            raw = parse_raw_delimiter(src, i)
            if raw is not None:
                flush("R")
                raw_delim, body_start = raw
                out.append(src[i:body_start])
                i = body_start
                state = "raw"
                continue

            if ch.isspace():
                if ch == "\n":
                    pending_newline = True
                    pending_space = False
                else:
                    if not pending_newline:
                        pending_space = True
                i += 1
                continue

            flush(ch)
            out.append(ch)

            if ch == '"':
                state = "string"
            elif ch == "'":
                state = "char"
            i += 1
            continue

        if state == "string":
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == '"':
                state = "normal"
            i += 1
            continue

        if state == "char":
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == "'":
                state = "normal"
            i += 1
            continue

        if state == "raw":
            end_seq = ")" + raw_delim + '"'
            if src.startswith(end_seq, i):
                out.append(end_seq)
                i += len(end_seq)
                state = "normal"
                continue
            out.append(ch)
            i += 1
            continue

    result = "".join(out)
    lines = [line for line in result.splitlines() if line.strip()]
    return "\n".join(lines) + "\n"


def run(input_path: Path, output_path: Path, max_bytes: int) -> int:
    source = input_path.read_text(encoding="utf-8")
    no_comments = strip_comments(source)
    minified = minify_whitespace(no_comments)

    output_path.write_text(minified, encoding="utf-8")

    in_size = input_path.stat().st_size
    out_size = output_path.stat().st_size

    print(f"Input : {input_path}")
    print(f"Output: {output_path}")
    print(f"Size  : {in_size} -> {out_size} bytes ({(out_size / in_size) * 100:.2f}% of original)")
    if out_size > max_bytes:
        print(f"Target not met: output is above {max_bytes} bytes")
        return 2
    print(f"Target met: output is <= {max_bytes} bytes")
    return 0


def main() -> int:
    default_in = Path("bot-development/bots/epic4-solver-bot.cpp")
    default_out = Path("bot-development/bots/epic4-solver-bot-trimmed.cpp")

    parser = argparse.ArgumentParser(description="C++ shipping minifier for bot source files")
    parser.add_argument("--input", default=str(default_in), help="Input C++ source file")
    parser.add_argument("--output", default=str(default_out), help="Output minified C++ source file")
    parser.add_argument("--max-bytes", type=int, default=100000, help="Maximum allowed output size")
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    if not input_path.exists():
        print(f"Input file not found: {input_path}")
        return 1

    output_path.parent.mkdir(parents=True, exist_ok=True)
    return run(input_path, output_path, args.max_bytes)


if __name__ == "__main__":
    ### usage
    # cd bot-development/bots
    #python3 cpp_shipping_minifier.py --input epic4-solver-bot.cpp --output epic4-solver-bot-trimmed.cpp
    raise SystemExit(main())