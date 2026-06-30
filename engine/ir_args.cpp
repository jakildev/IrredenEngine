#include <irreden/ir_args.hpp>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// IRArgs invariants are programmer errors (duplicate registration, typed-getter
// mismatch). We assert on a dependency-free macro rather than the engine's
// IR_ASSERT so the standalone tools — img_diff, jitter_probe, lua_codegen —
// can compile this translation unit without linking the engine profiler. The
// Release config defines NDEBUG alongside IR_RELEASE, so assert() and IR_ASSERT
// share the same compiled-out-in-release behavior; in debug both abort/throw on
// the same conditions.
#define IR_ASSERT(cond, msg) assert((cond) && (msg))

namespace IRArgs {

namespace {

// Per-type value placeholder shown after the arg name in --help. FLOAT_LIST
// repeats " <float>" once per declared value so the arity is visible.
std::string placeholderFor(Type type, int listCount) {
    switch (type) {
    case Type::FLAG:
        return "";
    case Type::INT:
        return " <int>";
    case Type::FLOAT:
        return " <float>";
    case Type::STRING:
        return " <value>";
    case Type::OPTIONAL_INT:
        return " [int]";
    case Type::FLOAT_LIST: {
        std::string placeholder;
        for (int k = 0; k < listCount; ++k) {
            placeholder += " <float>";
        }
        return placeholder;
    }
    }
    return "";
}

// Parse exactly `count` comma-separated floats from an inline "--name=a,b,c"
// value into `out`. Returns false when the token count doesn't match. A
// trailing comma (e.g. "8,") parses its empty tail as 0.0 rather than being
// rejected — count still matches, so this is accepted as malformed input
// (mirrors the equivalent pre-existing ambiguity in the space-separated form).
bool splitInlineFloatList(const std::string &value, int count, std::vector<float> &out) {
    out.clear();
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        out.push_back(static_cast<float>(std::atof(value.substr(start, end - start).c_str())));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return static_cast<int>(out.size()) == count;
}

// Split a "--name=value" long option into its name (before the first '=') and
// inline value (after). Returns true when an inline value was present; for any
// other token the whole token is the name and the value is empty. Splitting on
// the first '=' only, so a value may itself contain '='.
bool splitInlineValue(const char *token, std::string &name, std::string &value) {
    std::string t = token != nullptr ? token : "";
    if (t.rfind("--", 0) == 0) {
        const auto eq = t.find('=');
        if (eq != std::string::npos) {
            name = t.substr(0, eq);
            value = t.substr(eq + 1);
            return true;
        }
    }
    name = std::move(t);
    value.clear();
    return false;
}

} // namespace

Parser::Parser(const char *programDescription, Common common)
    : m_description(programDescription != nullptr ? programDescription : "") {
    // Built-in help — owned by the parser, free for every target.
    flag("--help", "Show this help and exit", "-h");
    if (common == Common::NONE) {
        // Standalone tool: only --help. Caller declares its own flags /
        // positionals; the engine-common args would be misleading noise.
        return;
    }
    // Engine-common args: every engine target that constructs a Parser inherits
    // these (and --help) without re-declaring. The canonical spellings are
    // preserved so existing command lines keep working.
    optionalInt(
        "--auto-screenshot",
        "Headless capture: cycle the shot table then exit; optional warmup frame count",
        kDefaultAutoScreenshotWarmup
    );
    string("--config-preset", "Path to a Lua config preset applied before init", "");
}

Parser::Entry &Parser::add(const char *name, const char *help, Type type, const char *shortAlias) {
    IR_ASSERT(
        name != nullptr && std::strncmp(name, "--", 2) == 0,
        "IRArgs: argument name must start with '--'"
    );
    IR_ASSERT(find(name) == nullptr, "IRArgs: duplicate argument registered");
    IR_ASSERT(
        shortAlias == nullptr || find(shortAlias) == nullptr,
        "IRArgs: duplicate short alias registered"
    );
    Entry entry;
    entry.name_ = name;
    entry.shortAlias_ = shortAlias != nullptr ? shortAlias : "";
    entry.help_ = help != nullptr ? help : "";
    entry.type_ = type;
    m_entries.push_back(std::move(entry));
    return m_entries.back();
}

Parser &Parser::flag(const char *name, const char *help, const char *shortAlias) {
    Entry &e = add(name, help, Type::FLAG, shortAlias);
    e.boolValue_ = false;
    return *this;
}

Parser &
Parser::integer(const char *name, const char *help, int defaultValue, const char *shortAlias) {
    Entry &e = add(name, help, Type::INT, shortAlias);
    e.intValue_ = defaultValue;
    return *this;
}

Parser &
Parser::number(const char *name, const char *help, float defaultValue, const char *shortAlias) {
    Entry &e = add(name, help, Type::FLOAT, shortAlias);
    e.floatValue_ = defaultValue;
    return *this;
}

Parser &Parser::string(
    const char *name, const char *help, const char *defaultValue, const char *shortAlias
) {
    Entry &e = add(name, help, Type::STRING, shortAlias);
    e.stringValue_ = defaultValue != nullptr ? defaultValue : "";
    return *this;
}

Parser &
Parser::optionalInt(const char *name, const char *help, int defaultIfBare, const char *shortAlias) {
    Entry &e = add(name, help, Type::OPTIONAL_INT, shortAlias);
    e.intValue_ = defaultIfBare;
    return *this;
}

Parser &Parser::numbers(const char *name, const char *help, int count, const char *shortAlias) {
    IR_ASSERT(count > 0, "IRArgs: numbers() requires a positive value count");
    Entry &e = add(name, help, Type::FLOAT_LIST, shortAlias);
    e.listCount_ = count;
    e.floatValues_.assign(static_cast<std::size_t>(count), 0.0f);
    return *this;
}

Parser &Parser::positional(const char *name, const char *help) {
    IR_ASSERT(
        m_positionals.empty() || !m_positionals.back().variadic_,
        "IRArgs: a fixed positional cannot follow a variadic one"
    );
    Positional p;
    p.name_ = name != nullptr ? name : "";
    p.help_ = help != nullptr ? help : "";
    m_positionals.push_back(std::move(p));
    return *this;
}

Parser &Parser::variadic(const char *name, const char *help, int minCount) {
    IR_ASSERT(
        m_positionals.empty() || !m_positionals.back().variadic_,
        "IRArgs: only one variadic positional, registered last"
    );
    Positional p;
    p.name_ = name != nullptr ? name : "";
    p.help_ = help != nullptr ? help : "";
    p.variadic_ = true;
    p.minCount_ = minCount;
    m_positionals.push_back(std::move(p));
    return *this;
}

Parser::Entry *Parser::find(const std::string &token) {
    for (Entry &e : m_entries) {
        if (e.name_ == token || (!e.shortAlias_.empty() && e.shortAlias_ == token)) {
            return &e;
        }
    }
    return nullptr;
}

const Parser::Entry *Parser::findByName(const char *name) const {
    for (const Entry &e : m_entries) {
        if (e.name_ == name) {
            return &e;
        }
    }
    return nullptr;
}

void Parser::parse(int argc, char **argv) {
    if (argc > 0 && argv[0] != nullptr) {
        std::string path = argv[0];
        const auto slash = path.find_last_of("/\\");
        m_programName = (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    // --help / -h takes precedence regardless of position, and must run before
    // any heavy init — so resolve it up front, before mutating any entry. This
    // also keeps usage() reporting the registered defaults rather than values
    // parsed from tokens that happened to precede --help on the line.
    for (int i = 1; i < argc; ++i) {
        std::string name;
        std::string value;
        splitInlineValue(argv[i], name, value);
        const Entry *e = find(name);
        if (e != nullptr && e->name_ == "--help") {
            exitWithUsage(0);
        }
    }

    m_positionalValues.clear();
    for (int i = 1; i < argc; ++i) {
        std::string name;
        std::string inlineValue;
        const bool hasInline = splitInlineValue(argv[i], name, inlineValue);
        Entry *e = find(name);
        if (e == nullptr) {
            // Not a registered named arg. A dash-led token is an unknown flag;
            // anything else is a positional (its count is validated below).
            if (!name.empty() && name[0] == '-') {
                std::fprintf(stderr, "Unknown argument: %s\n\n", argv[i]);
                exitWithUsage(2);
            }
            m_positionalValues.push_back(argv[i]);
            continue;
        }

        // The value for a value-taking arg comes inline ("--name=value") or as
        // the next token ("--name value"); a missing next token is an error.
        const auto valueFor = [&](const char *expects) -> std::string {
            if (hasInline) {
                return inlineValue;
            }
            if (i + 1 < argc) {
                return argv[++i];
            }
            std::fprintf(stderr, "Argument %s expects %s\n\n", name.c_str(), expects);
            exitWithUsage(2);
        };

        e->provided_ = true;
        switch (e->type_) {
        case Type::FLAG:
            if (hasInline) {
                std::fprintf(stderr, "Argument %s takes no value\n\n", name.c_str());
                exitWithUsage(2);
            }
            e->boolValue_ = true;
            break;
        case Type::INT:
            e->intValue_ = std::atoi(valueFor("an integer value").c_str());
            break;
        case Type::FLOAT:
            e->floatValue_ = static_cast<float>(std::atof(valueFor("a float value").c_str()));
            break;
        case Type::STRING:
            e->stringValue_ = valueFor("a value");
            break;
        case Type::OPTIONAL_INT:
            // An inline value is taken verbatim; otherwise consume the next
            // token only when it reads as a positive integer, leaving it for the
            // loop otherwise (existing command lines are byte-identical; the bare
            // default stays in intValue_).
            if (hasInline) {
                e->intValue_ = std::atoi(inlineValue.c_str());
            } else if (i + 1 < argc) {
                const int parsed = std::atoi(argv[i + 1]);
                if (parsed > 0) {
                    e->intValue_ = parsed;
                    ++i;
                }
            }
            break;
        case Type::FLOAT_LIST:
            // An inline value is one comma-separated token ("--name=a,b,c");
            // otherwise consume the next listCount_ tokens space-separated. Too
            // few tokens (or a wrong inline count) is a hard parse error so a
            // malformed sweep can't silently run with zeroed values.
            if (hasInline) {
                if (!splitInlineFloatList(inlineValue, e->listCount_, e->floatValues_)) {
                    std::fprintf(
                        stderr,
                        "Argument %s expects %d comma-separated float values\n\n",
                        name.c_str(),
                        e->listCount_
                    );
                    exitWithUsage(2);
                }
            } else {
                for (int k = 0; k < e->listCount_; ++k) {
                    if (i + 1 >= argc) {
                        std::fprintf(
                            stderr,
                            "Argument %s expects %d float values\n\n",
                            name.c_str(),
                            e->listCount_
                        );
                        exitWithUsage(2);
                    }
                    e->floatValues_[static_cast<std::size_t>(k)] =
                        static_cast<float>(std::atof(argv[++i]));
                }
            }
            break;
        }
    }

    // Validate positional count against the declarations.
    std::size_t fixedCount = 0;
    for (const Positional &p : m_positionals) {
        if (!p.variadic_) {
            ++fixedCount;
        }
    }
    const bool hasVariadic = !m_positionals.empty() && m_positionals.back().variadic_;
    const std::size_t minNeeded =
        fixedCount + (hasVariadic ? static_cast<std::size_t>(m_positionals.back().minCount_) : 0);
    if (m_positionalValues.size() < minNeeded) {
        std::fprintf(
            stderr,
            "Expected %s%zu positional argument(s), got %zu\n\n",
            hasVariadic ? "at least " : "",
            minNeeded,
            m_positionalValues.size()
        );
        exitWithUsage(2);
    }
    if (!hasVariadic && m_positionalValues.size() > fixedCount) {
        std::fprintf(
            stderr,
            "Unexpected positional argument: %s\n\n",
            m_positionalValues[fixedCount].c_str()
        );
        exitWithUsage(2);
    }
}

bool Parser::getFlag(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(
        e != nullptr && e->type_ == Type::FLAG,
        "IRArgs: getFlag on unregistered/non-flag arg"
    );
    return e != nullptr && e->boolValue_;
}

int Parser::getInt(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(
        e != nullptr && (e->type_ == Type::INT || e->type_ == Type::OPTIONAL_INT),
        "IRArgs: getInt on unregistered/non-int arg"
    );
    return e != nullptr ? e->intValue_ : 0;
}

float Parser::getFloat(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(
        e != nullptr && e->type_ == Type::FLOAT,
        "IRArgs: getFloat on unregistered/non-float arg"
    );
    return e != nullptr ? e->floatValue_ : 0.0f;
}

std::string Parser::getString(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(
        e != nullptr && e->type_ == Type::STRING,
        "IRArgs: getString on unregistered/non-string arg"
    );
    return e != nullptr ? e->stringValue_ : std::string{};
}

const std::vector<float> &Parser::getFloats(const char *name) const {
    static const std::vector<float> empty;
    const Entry *e = findByName(name);
    IR_ASSERT(
        e != nullptr && e->type_ == Type::FLOAT_LIST,
        "IRArgs: getFloats on unregistered/non-float-list arg"
    );
    return e != nullptr ? e->floatValues_ : empty;
}

bool Parser::wasProvided(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(e != nullptr, "IRArgs: wasProvided on unregistered arg");
    return e != nullptr && e->provided_;
}

std::string Parser::getPositional(const char *name) const {
    // Fixed positionals fill the first slots of m_positionalValues in
    // registration order (the variadic tail, if any, captures the rest), so a
    // declaration's index doubles as its value index.
    for (std::size_t i = 0; i < m_positionals.size(); ++i) {
        if (!m_positionals[i].variadic_ && m_positionals[i].name_ == name) {
            return i < m_positionalValues.size() ? m_positionalValues[i] : std::string{};
        }
    }
    IR_ASSERT(false, "IRArgs: getPositional on unregistered positional");
    return std::string{};
}

const std::vector<std::string> &Parser::positionalArgs() const {
    return m_positionalValues;
}

int Parser::autoScreenshotWarmupFrames() const {
    return wasProvided("--auto-screenshot") ? getInt("--auto-screenshot") : 0;
}

std::string Parser::configPreset() const {
    return getString("--config-preset");
}

std::string Parser::usage() const {
    // Sort by name for stable, scannable output; the registration order is an
    // implementation detail no reader should have to track.
    std::vector<const Entry *> sorted;
    sorted.reserve(m_entries.size());
    for (const Entry &e : m_entries) {
        sorted.push_back(&e);
    }
    std::sort(sorted.begin(), sorted.end(), [](const Entry *a, const Entry *b) {
        return a->name_ < b->name_;
    });

    // Build each left column ("--name, -a <value>") and find the widest so the
    // help text aligns into a second column. Positional names share the same
    // width so both sections line up.
    std::vector<std::string> leftCols;
    leftCols.reserve(sorted.size());
    std::size_t widest = 0;
    for (const Entry *e : sorted) {
        std::string left = e->name_;
        if (!e->shortAlias_.empty()) {
            left += ", " + e->shortAlias_;
        }
        left += placeholderFor(e->type_, e->listCount_);
        if (left.size() > widest) {
            widest = left.size();
        }
        leftCols.push_back(std::move(left));
    }
    for (const Positional &p : m_positionals) {
        if (p.name_.size() > widest) {
            widest = p.name_.size();
        }
    }

    // Usage line: positional placeholders trail the [options] marker, with the
    // variadic tail shown as "<name>...".
    std::string out = "Usage: " + m_programName + " [options]";
    for (const Positional &p : m_positionals) {
        out += " <" + p.name_ + (p.variadic_ ? ">..." : ">");
    }
    out += "\n";
    if (!m_description.empty()) {
        out += "\n" + m_description + "\n";
    }
    out += "\nOptions:\n";
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        out += "  " + leftCols[i];
        out += std::string(widest - leftCols[i].size() + 2, ' ');
        out += sorted[i]->help_;
        out += "\n";
    }
    if (!m_positionals.empty()) {
        out += "\nPositional arguments:\n";
        for (const Positional &p : m_positionals) {
            out += "  " + p.name_;
            out += std::string(widest - p.name_.size() + 2, ' ');
            out += p.help_;
            out += "\n";
        }
    }
    return out;
}

void Parser::exitWithUsage(int code) const {
    const std::string text = usage();
    std::fputs(text.c_str(), code == 0 ? stdout : stderr);
    std::exit(code);
}

} // namespace IRArgs
