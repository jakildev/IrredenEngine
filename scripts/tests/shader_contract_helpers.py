"""Extract brace blocks and scalar/vector functions for executable shader controls."""

import re


def extract_block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError(f"unclosed block {marker}")
    return source[start:end]


def extract_function(source, name):
    match = re.search(
        r"^[ \t]*(?:(?:inline|constexpr|static) )*(?:void|bool|uint|int|float|vec[234]|float[234]) "
        + re.escape(name) + r"\([^)]*\)\s*\{", source, re.MULTILINE)
    if match is None:
        raise ValueError(f"missing function {name}")
    return extract_block(source, match.group())
