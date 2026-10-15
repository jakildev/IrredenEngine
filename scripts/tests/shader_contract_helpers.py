"""Extract shader bodies and apply checked mutations for executable controls."""

import re


def compact(source):
    return " ".join(source.split())


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError(f"expected one mutation site for {before!r}")
    return source.replace(before, after)


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


def shape_owner_contract(source):
    """Split a shape emitter's ownership text into C++ over depthEncoded, stored,
    sampleOwner, elected and xrayOccluded: the owner key, the candidate-bit
    declaration, both publish predicates and the election body."""
    def elected(text):
        text = re.sub(r"atomic_load_explicit\(\s*&sampleOwners\[linearIndex\],\s*"
                      r"memory_order_relaxed\s*\)", "elected", text)
        return text.replace("sampleOwners[linearIndex]", "elected")

    election = extract_block(source, "#if IR_SHAPE_PASS == 3")
    election, loads = re.subn(r"(?:const )?int stored = [^;]+;", "",
                              election[election.index("{"):])
    if loads != 1:
        raise ValueError("expected one stored-depth load in the election")
    election, contenders = re.subn(
        r"(?:atomicMin\(elected, ([^;]+?)\)|"
        r"atomic_fetch_min_explicit\(&elected, ([^;]+?), memory_order_relaxed\));",
        lambda match: f"fixtureAtomicMin(elected, {match.group(1) or match.group(2)});",
        elected(election))
    if contenders != 2:
        raise ValueError("expected an owner and an x-ray contender in the election")
    return {
        "key": re.search(r"const uint sampleOwner = ([^;]+);", source).group(1),
        "candidate_bit": re.search(r"(?:const|constant) uint kXrayCandidateBit = [^;]+;",
                                   source).group().replace("constant ", "const "),
        "owner_predicate": elected(re.search(
            r"if \((depthEncoded == stored &&.*?)\) \{", source, re.DOTALL).group(1)),
        "xray_predicate": elected(re.search(
            r"else if \((xrayOccluded && depthEncoded > stored &&.*?)\) \{",
            source, re.DOTALL).group(1)),
        "election": election,
    }
