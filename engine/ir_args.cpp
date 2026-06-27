#include <irreden/ir_args.hpp>

#include <irreden/ir_profile.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace IRArgs {

namespace {

// Per-type value placeholder shown after the arg name in --help.
const char *placeholderFor(Type type) {
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
    }
    return "";
}

} // namespace

Parser::Parser(const char *programDescription)
    : m_description(programDescription != nullptr ? programDescription : "") {
    // Built-in help — owned by the parser, free for every target.
    flag("--help", "Show this help and exit", "-h");
    // Engine-common args: every target that constructs a Parser inherits these
    // (and --help) without re-declaring. The canonical spellings are preserved
    // so existing command lines keep working.
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
        const Entry *e = find(argv[i]);
        if (e != nullptr && e->name_ == "--help") {
            exitWithUsage(0);
        }
    }

    for (int i = 1; i < argc; ++i) {
        Entry *e = find(argv[i]);
        if (e == nullptr) {
            std::fprintf(stderr, "Unknown argument: %s\n\n", argv[i]);
            exitWithUsage(2);
        }
        e->provided_ = true;
        switch (e->type_) {
        case Type::FLAG:
            e->boolValue_ = true;
            break;
        case Type::INT:
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Argument %s expects an integer value\n\n", argv[i]);
                exitWithUsage(2);
            }
            e->intValue_ = std::atoi(argv[++i]);
            break;
        case Type::FLOAT:
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Argument %s expects a float value\n\n", argv[i]);
                exitWithUsage(2);
            }
            e->floatValue_ = static_cast<float>(std::atof(argv[++i]));
            break;
        case Type::STRING:
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Argument %s expects a value\n\n", argv[i]);
                exitWithUsage(2);
            }
            e->stringValue_ = argv[++i];
            break;
        case Type::OPTIONAL_INT:
            // Consume the next token only when it reads as a positive integer,
            // otherwise leave it for the loop. Mirrors the historical
            // parseAutoScreenshotArgv peek so existing command lines are
            // byte-identical (the bare default stays in intValue_).
            if (i + 1 < argc) {
                const int parsed = std::atoi(argv[i + 1]);
                if (parsed > 0) {
                    e->intValue_ = parsed;
                    ++i;
                }
            }
            break;
        }
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

bool Parser::wasProvided(const char *name) const {
    const Entry *e = findByName(name);
    IR_ASSERT(e != nullptr, "IRArgs: wasProvided on unregistered arg");
    return e != nullptr && e->provided_;
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
    // help text aligns into a second column.
    std::vector<std::string> leftCols;
    leftCols.reserve(sorted.size());
    std::size_t widest = 0;
    for (const Entry *e : sorted) {
        std::string left = e->name_;
        if (!e->shortAlias_.empty()) {
            left += ", " + e->shortAlias_;
        }
        left += placeholderFor(e->type_);
        if (left.size() > widest) {
            widest = left.size();
        }
        leftCols.push_back(std::move(left));
    }

    std::string out = "Usage: " + m_programName + " [options]\n";
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
    return out;
}

void Parser::exitWithUsage(int code) const {
    const std::string text = usage();
    std::fputs(text.c_str(), code == 0 ? stdout : stderr);
    std::exit(code);
}

} // namespace IRArgs
