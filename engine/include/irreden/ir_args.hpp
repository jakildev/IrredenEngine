#ifndef IR_ARGS_H
#define IR_ARGS_H

#include <string>
#include <vector>

// IRArgs — declarative command-line argument framework.
//
//     IRArgs::Parser args("my_demo — what it does.");
//     args.flag("--no-overlay", "Hide the debug overlay");
//     args.integer("--grid-size", "Grid edge in cells", 64);
//     args.parse(argc, argv);
//     if (args.getFlag("--no-overlay")) ...
//     int n = args.getInt("--grid-size");
//
// Standalone tools that don't run the engine loop construct the parser in
// no-common-args mode (so --help doesn't advertise --auto-screenshot /
// --config-preset) and take positional arguments:
//
//     IRArgs::Parser args("img_diff — highlight PNG drift.", IRArgs::Common::NONE);
//     args.integer("--threshold", "Per-channel tolerance", 0);
//     args.positional("baseline", "Baseline PNG");
//     args.positional("current", "Current PNG");
//     args.positional("out_diff", "Output diff PNG");
//     args.parse(argc, argv);
//     std::string base = args.getPositional("baseline");
namespace IRArgs {

// The engine-common --auto-screenshot warmup default when the flag is given
// without a trailing frame count.
inline constexpr int kDefaultAutoScreenshotWarmup = 10;

// Kind of a registered argument. Drives both the parse rule and the value
// placeholder shown in --help.
enum class Type {
    FLAG,         // boolean switch, no value           (--no-overlay)
    INT,          // takes an integer value             (--grid-size 64)
    FLOAT,        // takes a float value                (--zoom 4.0)
    STRING,       // takes a string value               (--config-preset path.lua)
    OPTIONAL_INT, // switch with an optional trailing int (--auto-screenshot [frames])
    FLOAT_LIST,   // takes a fixed count of float values  (--sweep-yaw 0 360 8)
};

// Which built-in args the constructor pre-registers. ENGINE (the default) adds
// the engine-common args; NONE adds only --help / -h, for standalone tools that
// don't run the engine loop and shouldn't advertise --auto-screenshot /
// --config-preset.
enum class Common {
    ENGINE,
    NONE,
};

class Parser {
  public:
    // `programDescription` is an optional one-line summary printed under the
    // usage header. The constructor always pre-registers the built-in --help /
    // -h. With Common::ENGINE (the default) it also pre-registers the
    // engine-common args (--auto-screenshot, --config-preset) so every engine
    // target inherits them without re-declaring; with Common::NONE it does not,
    // for standalone tools (see the header example).
    explicit Parser(const char *programDescription = nullptr, Common common = Common::ENGINE);

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
    // A flag that consumes exactly `count` float values, given space-separated
    // ("--sweep-yaw 0 360 8") or as one comma-separated inline token
    // ("--sweep-yaw=0,360,8"). `count` must be positive; too few values (or a
    // wrong inline count) is a parse error. Read back with getFloats(); each
    // value defaults to 0 until provided. Integer-valued slots are recovered by
    // the caller via static_cast<int> on the float.
    Parser &
    numbers(const char *name, const char *help, int count, const char *shortAlias = nullptr);

    // Positional arguments (ordered, no leading dash). Register one fixed
    // positional with `positional`; declare a trailing variable-count tail with
    // `variadic` (which must be the last positional registered and captures all
    // remaining positionals, requiring at least `minCount`). `name` is the
    // placeholder shown in --help; access values after parse() via
    // getPositional(name) (fixed) or positionalArgs() (all, in order).
    Parser &positional(const char *name, const char *help);
    Parser &variadic(const char *name, const char *help, int minCount = 0);

    // Parse argv (argv[0] is the program path and is skipped). Value-taking args
    // accept both "--name value" and "--name=value". On --help / -h: print usage
    // to stdout and exit(0). On an unknown arg, a missing value, or too few /
    // many positionals: print the diagnostic + usage to stderr and exit(2).
    void parse(int argc, char **argv);

    // Typed access, valid after parse(). Each returns the registered default
    // when the arg was not supplied. A name/type mismatch asserts in debug.
    bool getFlag(const char *name) const;
    int getInt(const char *name) const;
    float getFloat(const char *name) const;
    std::string getString(const char *name) const;
    // The `count` floats bound to a FLOAT_LIST arg (registered via numbers()),
    // in command-line order. Returns the registered all-zero defaults when the
    // arg was not supplied.
    const std::vector<float> &getFloats(const char *name) const;

    // True when the arg appeared on the command line (vs. resolved to its
    // default). The honest "present?" signal for OPTIONAL_INT switches such as
    // --auto-screenshot, where presence alone changes behavior.
    bool wasProvided(const char *name) const;

    // Positional access, valid after parse(). getPositional returns the value
    // bound to a fixed positional by name (asserts in debug if `name` wasn't
    // registered via positional()). positionalArgs returns every positional
    // token in command-line order, including the variadic tail.
    std::string getPositional(const char *name) const;
    const std::vector<std::string> &positionalArgs() const;

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
        std::vector<float> floatValues_; // FLOAT_LIST values (sized to listCount_)
        int listCount_ = 0;              // FLOAT_LIST arity
        bool provided_ = false;
    };

    // A declared positional argument. The `variadic_` tail captures all
    // positionals past the fixed ones (>= minCount_).
    struct Positional {
        std::string name_;
        std::string help_;
        bool variadic_ = false;
        int minCount_ = 0;
    };

    Entry &add(const char *name, const char *help, Type type, const char *shortAlias);
    Entry *find(const std::string &token);
    const Entry *findByName(const char *name) const;
    [[noreturn]] void exitWithUsage(int code) const;

    std::string m_description;
    std::string m_programName = "program";
    std::vector<Entry> m_entries;
    std::vector<Positional> m_positionals;       // declared, fixed then optional variadic tail
    std::vector<std::string> m_positionalValues; // captured at parse, in order
};

} // namespace IRArgs

#endif /* IR_ARGS_H */
