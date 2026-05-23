// T-107: DSL parser + C++ emitter for Lua system bodies in CODEGEN mode.
//
// The codegen tool runs the creation's `.lua` files via sol2 (T-106). When
// `IRSystem.registerSystem({...})` is called, the shim captures the
// system metadata (name, components, excludes) and the source location of
// the `tick = function(arch) ... end` block via the Lua debug API. This
// header defines the AST + parser + emitter that turn the body source into
// a `template <> struct System<NAME>`-shaped C++ specialisation.
//
// The DSL subset is intentionally narrow (#587 §"DSL subset"): canonical
// `for i = 0, arch.length - 1 do` loop, column ops on Lua-defined
// components, math + comparisons + branches, locals, and a whitelisted
// intrinsic registry. Anything else is a build-time error pointing at
// file/line/feature.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace IRLuaCodegen {

// ---- Component registry forwarded from the components capture ---------------
//
// The systems emitter needs to know which component names were declared
// via `IRComponent.register(...)` so it can validate column-ops references
// and translate Lua field names to C++ field names (with the `_` suffix).

enum class FieldType { INT32, FLOAT, BOOL, STRING };

struct ComponentField {
    std::string name_;
    FieldType type_;
};

struct ComponentSchema {
    std::string name_;                        // Lua name, e.g. "Hp"
    std::string sourceFile_;
    std::vector<ComponentField> fields_;      // alphabetically sorted (T-106 invariant)
};

// ---- Tokens -----------------------------------------------------------------

enum class TokenKind {
    // Literals
    NAME,           // identifier
    NUMBER_INT,     // integer literal (Lua 5.4 / LuaJIT integer subtype, or no decimal point)
    NUMBER_FLOAT,   // floating-point literal (decimal point or exponent)
    STRING,         // quoted string literal

    // Keywords
    KW_AND, KW_OR, KW_NOT,
    KW_IF, KW_THEN, KW_ELSE, KW_ELSEIF, KW_END,
    KW_FOR, KW_DO, KW_WHILE, KW_REPEAT, KW_UNTIL, KW_BREAK, KW_GOTO,
    KW_FUNCTION, KW_LOCAL, KW_RETURN,
    KW_TRUE, KW_FALSE, KW_NIL,
    KW_IN,

    // Operators / punctuation
    PLUS, MINUS, STAR, SLASH, PERCENT, CARET,
    LT, GT, LE, GE, EQ, NEQ, ASSIGN,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COMMA, SEMI, COLON, DOT, ELLIPSIS, HASH,

    // End
    END_OF_INPUT,
};

struct Token {
    TokenKind kind_;
    std::string text_;       // raw text for NAME / numbers / strings
    int line_;               // 1-based; relative to the source file the body came from
};

// ---- AST --------------------------------------------------------------------

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

// Expression kinds the DSL accepts.
enum class ExprKind {
    NUMBER_LITERAL,    // 1, 3.14
    BOOL_LITERAL,      // true / false
    STRING_LITERAL,    // "x"
    NAME_REF,          // identifier — could be local var, global, intrinsic root
    UNARY_OP,          // -x, not x
    BINARY_OP,         // a + b, a == b, a and b
    INDEX,             // obj.field   (table-index by string key; method args go through CALL)
    INTRINSIC_CALL,    // math.sin(x), IRMath.lerp(a, b, t)
    COLUMN_AT,         // arch.Comp:at(i)
    COLUMN_GET_FIELD,  // arch.Comp:getField(i, "field")
    COMPONENT_NEW,     // Comp.new(args...)
};

enum class BinOp {
    ADD, SUB, MUL, DIV, MOD,
    LT, GT, LE, GE, EQ, NEQ,
    AND, OR,
};

enum class UnOp { NEG, NOT };

struct Expr {
    ExprKind kind_;
    int line_ = 0;

    // Tagged union via discrete fields (avoids std::variant complexity for v1).
    // Only the subset relevant to `kind_` is populated.

    // NUMBER_LITERAL
    bool isInt_ = false;
    std::int64_t intValue_ = 0;
    double floatValue_ = 0.0;

    // BOOL_LITERAL
    bool boolValue_ = false;

    // STRING_LITERAL
    std::string stringValue_;

    // NAME_REF
    std::string name_;

    // UNARY_OP / BINARY_OP
    UnOp unOp_ = UnOp::NEG;
    BinOp binOp_ = BinOp::ADD;
    ExprPtr lhs_;
    ExprPtr rhs_;

    // INDEX: receiver_.field_
    ExprPtr receiver_;
    std::string field_;

    // INTRINSIC_CALL: e.g. math.sin(x)
    //   intrinsicNamespace_ = "math", intrinsicName_ = "sin"
    //   args_ holds the argument expressions
    std::string intrinsicNamespace_;
    std::string intrinsicName_;

    // COLUMN_AT: arch.Comp:at(i)
    //   componentName_ = "Comp"
    //   args_ holds the index expression
    std::string componentName_;

    // COLUMN_GET_FIELD: arch.Comp:getField(i, "field")
    //   componentName_, fieldNameLiteral_ ("field" — must be a string literal),
    //   args_[0] holds the index expression

    // COMPONENT_NEW: Comp.new(a, b)
    //   componentName_ = "Comp"
    //   args_ holds the constructor arguments (in alphabetical order — same
    //   as T-106's constructor emission)

    std::string fieldNameLiteral_;
    std::vector<ExprPtr> args_;
};

enum class StmtKind {
    LOCAL_DECL,         // local x = expr
    ASSIGN,             // x = expr,  obj.field = expr,  arch.Comp:setAt(i, val), arch.Comp:setField(i, "f", val)
    IF,                 // if .. then .. (elseif .. then ..)* (else ..)? end
    FOR_NUMERIC,        // for i = lo, hi do ... end
    EXPR_STMT,          // standalone call, e.g. doSomething()  — rare in DSL but kept for completeness
    RETURN,             // return [expr]   (allowed only at body's end; v1 forbids inside loops)
};

// LHS of an assignment. Either a local name or a column-write (setAt/setField).
enum class AssignTargetKind {
    NAME,                  // bare name = expr
    COLUMN_SET_AT,         // arch.Comp:setAt(i, value)  — value is the rhs
    COLUMN_SET_FIELD,      // arch.Comp:setField(i, "field", value)  — value is the rhs
};

struct AssignTarget {
    AssignTargetKind kind_;
    std::string name_;                 // for NAME
    std::string componentName_;        // for COLUMN_SET_AT / COLUMN_SET_FIELD
    std::string fieldNameLiteral_;     // for COLUMN_SET_FIELD
    ExprPtr indexExpr_;                // for COLUMN_SET_AT / COLUMN_SET_FIELD
    int line_ = 0;
};

struct IfBranch {
    ExprPtr cond_;
    std::vector<StmtPtr> body_;
};

struct Stmt {
    StmtKind kind_;
    int line_ = 0;

    // LOCAL_DECL: local <name> = <expr>
    std::string localName_;
    ExprPtr localInit_;

    // ASSIGN
    AssignTarget assignTarget_;
    ExprPtr assignRhs_;

    // IF
    std::vector<IfBranch> ifBranches_;          // index 0 = the `if`, 1+ = `elseif`s
    std::vector<StmtPtr> elseBody_;
    bool hasElse_ = false;

    // FOR_NUMERIC: for <var> = <lo>, <hi> do <body> end
    std::string forVar_;
    ExprPtr forLo_;
    ExprPtr forHi_;
    std::vector<StmtPtr> forBody_;

    // EXPR_STMT
    ExprPtr exprStmt_;

    // RETURN
    ExprPtr returnExpr_;
    bool hasReturnExpr_ = false;
};

// A parsed system body: ordered list of statements. The canonical loop
// (`for i = 0, arch.length - 1 do ... end`) typically lives at the top
// level of the body, but the parser does not enforce that — emit-time
// validation handles the structural checks.
struct ParsedBody {
    std::vector<StmtPtr> stmts_;
};

// ---- Captured system metadata (filled by main.cpp's IRSystem shim) ----------

// Per-system mode resolved by the codegen tool. CODEGEN emits a typed C++
// `IRSystem::createSystem<...>` specialisation; EVAL skips C++ emission and
// the system is left to register at runtime via the existing Lua-driven path
// (`IRSystem.registerSystem` → `IRSystem::createSystemDynamic`). See the
// per-system `mode` field in `engine/script/CLAUDE.md` and the design at
// `docs/design/lua-driven-ecs.md`.
enum class SystemMode { CODEGEN, EVAL };

// Mirrors `IRSystem::Concurrency` (engine/system/include/irreden/system/
// ir_system_types.hpp). Kept as a separate enum here so system_dsl.hpp
// stays free of an engine-header include — the codegen tool's link surface
// is sol2 + Lua, not the engine static libraries.
// Values must match IRSystem::Concurrency in engine/system/include/irreden/system/ir_system_types.hpp
enum class Concurrency { SERIAL = 0, PARALLEL_FOR = 1, MAIN_THREAD = 2 };

struct SystemRecord {
    std::string name_;                       // e.g. "MoveByVelocity"
    SystemMode mode_ = SystemMode::CODEGEN;  // per-system mode override
    Concurrency concurrency_ = Concurrency::SERIAL;  // T-223 opt-in
    std::vector<std::string> components_;    // include archetype, in declared order
    std::vector<std::string> excludes_;      // exclude archetype, in declared order
    std::string sourceFile_;                 // resolved file path
    int linedefined_ = 0;
    int lastlinedefined_ = 0;
    std::string bodySource_;                 // sliced source for the function body (between `(arch)` and `end`)
    int bodyStartLine_ = 0;                  // line of bodySource_[0] in the original file (so error reports map back)
};

// ---- Parser + emitter API ---------------------------------------------------

struct ParseError {
    std::string message_;
    std::string file_;
    int line_ = 0;
};

// Parse a system body source string into a ParsedBody.
//
// The `bodyStartLine` argument is the line in `sourceFile` that bodySource[0]
// corresponds to — used for error messages. Throws ParseError on rejection.
ParsedBody parseSystemBody(
    const std::string &sourceFile,
    int bodyStartLine,
    const std::string &bodySource
);

// Validate + emit a single system as a C++ create function.
//
// Writes to `out` a self-contained `inline IRSystem::SystemId
// createSystem_<NAME>()` function whose body calls `IRSystem::createSystem<...>`
// with the translated tick lambda. Throws ParseError if the body references
// a component not in `componentRegistry`, calls a non-whitelisted intrinsic,
// or otherwise violates the DSL.
//
// `componentRegistry` is the set of components captured during the same
// codegen run (Lua-defined via `IRComponent.register`). The systems emitter
// rejects any column-op against a name not in this registry.
void emitSystem(
    std::string &out,
    const SystemRecord &record,
    const ParsedBody &body,
    const std::vector<ComponentSchema> &componentRegistry
);

// Slice the body source from a file given the `(arch)` parameter list end
// and the closing `end`. Used by main.cpp once it has the linedefined /
// lastlinedefined positions from `lua_getinfo`.
//
// Returns the substring between the first `)` after `function` on linedefined
// and the matching `end` on lastlinedefined. Sets `outBodyStartLine` to the
// line in the source file where the slice starts.
std::string sliceFunctionBody(
    const std::string &sourceFile,
    int linedefined,
    int lastlinedefined,
    int &outBodyStartLine
);

// Whitelisted intrinsic registry. Each entry maps a Lua call (namespace.name)
// to a C++ emission template (e.g. `IRMath::sin`) and an arity.
struct Intrinsic {
    const char *luaNamespace_;     // "math", "IRMath"
    const char *luaName_;           // "sin", "lerp"
    const char *cppExpression_;     // "IRMath::sin"  (called as cppExpression_(arg0, arg1, ...))
    int arity_;                     // -1 for variadic; v1 uses fixed arities only
};

const std::vector<Intrinsic> &intrinsicRegistry();

// Look up an intrinsic by namespace + name. Returns nullptr on miss.
const Intrinsic *findIntrinsic(const std::string &ns, const std::string &name);

} // namespace IRLuaCodegen
