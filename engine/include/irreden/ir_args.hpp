#ifndef IR_ARGS_H
#define IR_ARGS_H

#include <string>
#include <vector>

// IRArgs — declarative command-line argument framework with a built-in
// --help / -h for every launch target.
//
// Before this, every demo hand-rolled its own `for (i…) std::strcmp(...)`
// parse loop, no target had a --help, and the two engine helpers
// (IRVideo::parseAutoScreenshotArgv, IREngine::parseConfigPresetArg) lived
// scattered apart. A target instead constructs an IRArgs::Parser (which
// pre-registers the engine-common args + --help), declares its own args, then
// calls parse():
//
//     IRArgs::Parser args("my_demo — what it does.");
//     args.flag("--no-overlay", "Hide the debug overlay");
//     args.integer("--grid-size", "Grid edge in cells", 64);
//     args.parse(argc, argv);
//     if (args.getFlag("--no-overlay")) ...
//     int n = args.getInt("--grid-size");
//
// --help / -h prints the auto-generated usage (all registered args) and exits
// 0; an unknown argument prints an error + usage and exits 2. parse() must run
// before any heavy init (window / GL / Metal) so `MyDemo --help` is instant and
// headless-safe.
//
// The header is intentionally light (only <string> / <vector>) because every
// target includes it; all logic lives in ir_args.cpp.
namespace IRArgs {

// The engine-common --auto-screenshot warmup default when the flag is given
// without a trailing frame count. Mirrors the long-standing default in
// IRVideo::parseAutoScreenshotArgv.
inline constexpr int kDefaultAutoScreenshotWarmup = 10;

// Kind of a registered argument. Drives both the parse rule and the value
// placeholder shown in --help.
enum class Type {
    FLAG,         // boolean switch, no value           (--no-overlay)
    INT,          // takes an integer value             (--grid-size 64)
    FLOAT,        // takes a float value                (--zoom 4.0)
    STRING,       // takes a string value               (--config-preset path.lua)
    OPTIONAL_INT, // switch with an optional trailing int (--auto-screenshot [frames])
};

class Parser {
  public:
    // `programDescription` is an optional one-line summary printed under the
    // usage header. The constructor pre-registers the built-in --help / -h and
    // the engine-common args (--auto-screenshot, --config-preset), so every
    // target inherits them without re-declaring.
    explicit Parser(const char *programDescription = nullptr);

    // Declarative registration. All return *this so calls can chain.
    // `name` must start with "--"; `shortAlias` is an optional single-dash
    // alias such as "-z" (nullptr for none). Registering a duplicate name
    // asserts in debug.
    Parser &flag(const char *name, const char *help, const char *shortAlias = nullptr);
    Parser &
    integer(const char *name, const char *help, int defaultValue, const char *shortAlias = nullptr);
    Parser &number(
        const char *name, const char *help, float defaultValue, const char *shortAlias = nullptr
    );
    Parser &string(
        const char *name,
        const char *help,
        const char *defaultValue,
        const char *shortAlias = nullptr
    );
    // A switch that may carry an optional trailing positive integer; when the
    // value is omitted the arg resolves to `defaultIfBare`. wasProvided() still
    // reports whether the switch itself appeared.
    Parser &optionalInt(
        const char *name, const char *help, int defaultIfBare, const char *shortAlias = nullptr
    );

    // Parse argv (argv[0] is the program path and is skipped). On --help / -h:
    // print usage to stdout and exit(0). On an unknown arg or a missing value:
    // print the diagnostic + usage to stderr and exit(2).
    void parse(int argc, char **argv);

    // Typed access, valid after parse(). Each returns the registered default
    // when the arg was not supplied. A name/type mismatch asserts in debug.
    bool getFlag(const char *name) const;
    int getInt(const char *name) const;
    float getFloat(const char *name) const;
    std::string getString(const char *name) const;

    // True when the arg appeared on the command line (vs. resolved to its
    // default). The honest "present?" signal for OPTIONAL_INT switches such as
    // --auto-screenshot, where presence alone changes behavior.
    bool wasProvided(const char *name) const;

    // Engine-common convenience accessors — read the pre-registered common args
    // with their canonical semantics so every target drives them identically:
    //   - 0 when --auto-screenshot is absent (the established "not requested"
    //     sentinel), else the warmup frame count.
    //   - empty string when --config-preset is absent, else the path.
    int autoScreenshotWarmupFrames() const;
    std::string configPreset() const;

    // The auto-generated usage text (exactly what --help prints).
    std::string usage() const;

  private:
    struct Entry {
        std::string name_;
        std::string shortAlias_;
        std::string help_;
        Type type_;
        bool boolValue_ = false;
        int intValue_ = 0;
        float floatValue_ = 0.0f;
        std::string stringValue_;
        bool provided_ = false;
    };

    Entry &add(const char *name, const char *help, Type type, const char *shortAlias);
    Entry *find(const std::string &token);
    const Entry *findByName(const char *name) const;
    [[noreturn]] void exitWithUsage(int code) const;

    std::string m_description;
    std::string m_programName = "program";
    std::vector<Entry> m_entries;
};

} // namespace IRArgs

#endif /* IR_ARGS_H */
