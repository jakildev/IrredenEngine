"""Extract scalar/vector functions for executable shader contract controls."""

import re


def extract_function(source, name):
    match = re.search(
        r"^[ \t]*(?:(?:inline|constexpr) )?(?:bool|uint|int|float|vec[234]|float[234]) "
        + re.escape(name) + r"\([^)]*\)\s*\{", source, re.MULTILINE)
    if match is None:
        raise ValueError(f"missing function {name}")
    depth, end = 1, match.end()
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError(f"unclosed function {name}")
    return source[match.start():end]
