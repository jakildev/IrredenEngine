// T-107: tokenizer + recursive-descent parser + C++ emitter for the
// CODEGEN-mode Lua system body DSL. See system_dsl.hpp for the surface
// contract and #587 for the DSL spec.

#include "system_dsl.hpp"

#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace IRLuaCodegen {

namespace {

// ---- Intrinsic registry ----------------------------------------------------
//
// Maps Lua-side `namespace.name` to a C++ expression head. The arity is the
// number of arguments enforced at parse time. Anything not in this table is
// a build-time error pointing at file/line/feature; new entries are one line
// each, by design.
const std::vector<Intrinsic> kIntrinsicRegistry = {
    // --- math.* ---
    {"math", "sin",   "IRMath::sin",   1},
    {"math", "cos",   "IRMath::cos",   1},
    {"math", "sqrt",  "IRMath::sqrt",  1},
    {"math", "abs",   "IRMath::abs",   1},
    {"math", "floor", "IRMath::floor", 1},
    {"math", "ceil",  "IRMath::ceil",  1},
    {"math", "min",   "IRMath::min",   2},
    {"math", "max",   "IRMath::max",   2},
    // --- IRMath.* ---
    {"IRMath", "clamp", "IRMath::clamp", 3},
    // --- IRRender.* render-glue setters (#1616) ----------------------------
    //
    // Whitelisted side-effecting (void) engine bindings: allowed as a bare
    // statement in a CODEGEN tick body (`IRRender.setSunIntensity(x)`), lowered
    // to the C++ free function. These are pure pass-throughs to RenderManager —
    // no new engine surface — so a system that *computes* a render parameter
    // under CODEGEN can also *apply* it without falling back to EVAL / a C++
    // bridge. Scalar args only (the DSL lowers each arg as a numeric
    // expression). `true` marks statement-position-only; the trailing header
    // is emitted as an #include in the generated header so the call compiles.
    {"IRRender", "setSunIntensity", "IRRender::setSunIntensity", 1, true, "irreden/ir_render.hpp"},
    {"IRRender", "setSunAmbient",   "IRRender::setSunAmbient",   1, true, "irreden/ir_render.hpp"},
    {"IRRender", "setExposure",     "IRRender::setExposure",     1, true, "irreden/ir_render.hpp"},
    {"IRRender", "setSkyIntensity", "IRRender::setSkyIntensity", 1, true, "irreden/ir_render.hpp"},
    {"IRRender", "setCameraZoom",   "IRRender::setCameraZoom",   1, true, "irreden/ir_render.hpp"},
};

[[noreturn]] void fail(const std::string &file, int line, const std::string &msg) {
    ParseError err;
    err.message_ = msg;
    err.file_ = file;
    err.line_ = line;
    throw err;
}

// ---- Tokenizer -------------------------------------------------------------
//
// Lua-flavored lexer for the DSL subset. The full Lua grammar has more
// constructs (long strings, hex literals, label `::`), but the DSL bans
// them so the lexer rejects any unrecognized character with a clear error.

struct Lexer {
    const std::string &src_;
    const std::string &file_;
    int firstLineInFile_;     // line in the original file that src_[0] occupies
    size_t pos_ = 0;
    int line_ = 0;             // running line number in src_, offset by firstLineInFile_ for diagnostics

    Lexer(const std::string &src, const std::string &file, int firstLineInFile)
        : src_(src), file_(file), firstLineInFile_(firstLineInFile), line_(firstLineInFile) {}

    bool eof() const { return pos_ >= src_.size(); }
    char peek(size_t off = 0) const {
        return (pos_ + off < src_.size()) ? src_[pos_ + off] : '\0';
    }
    void advance() {
        if (pos_ < src_.size()) {
            if (src_[pos_] == '\n') ++line_;
            ++pos_;
        }
    }

    // Skip whitespace and Lua comments. Comments are `-- ...` to end of line.
    // Long-form `--[[ ... ]]` is intentionally rejected (DSL ban — keeps the
    // lexer simple and reviewer-friendly).
    void skipTrivia() {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else if (c == '-' && peek(1) == '-') {
                if (peek(2) == '[' && peek(3) == '[') {
                    fail(file_, line_,
                         "long-form `--[[ ]]` comments are not supported in CODEGEN bodies");
                }
                advance();
                advance();
                while (!eof() && peek() != '\n') advance();
            } else {
                break;
            }
        }
    }

    static bool isNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
    static bool isNameCont(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

    static const std::unordered_map<std::string, TokenKind> &keywords() {
        static const std::unordered_map<std::string, TokenKind> kKeywords = {
            {"and", TokenKind::KW_AND},
            {"or", TokenKind::KW_OR},
            {"not", TokenKind::KW_NOT},
            {"if", TokenKind::KW_IF},
            {"then", TokenKind::KW_THEN},
            {"else", TokenKind::KW_ELSE},
            {"elseif", TokenKind::KW_ELSEIF},
            {"end", TokenKind::KW_END},
            {"for", TokenKind::KW_FOR},
            {"do", TokenKind::KW_DO},
            {"while", TokenKind::KW_WHILE},
            {"repeat", TokenKind::KW_REPEAT},
            {"until", TokenKind::KW_UNTIL},
            {"break", TokenKind::KW_BREAK},
            {"goto", TokenKind::KW_GOTO},
            {"function", TokenKind::KW_FUNCTION},
            {"local", TokenKind::KW_LOCAL},
            {"return", TokenKind::KW_RETURN},
            {"true", TokenKind::KW_TRUE},
            {"false", TokenKind::KW_FALSE},
            {"nil", TokenKind::KW_NIL},
            {"in", TokenKind::KW_IN},
        };
        return kKeywords;
    }

    Token nextToken() {
        skipTrivia();
        if (eof()) return Token{TokenKind::END_OF_INPUT, "", line_};

        const int startLine = line_;
        const char c = peek();

        // Identifier / keyword
        if (isNameStart(c)) {
            std::string ident;
            while (!eof() && isNameCont(peek())) {
                ident.push_back(peek());
                advance();
            }
            const auto &kw = keywords();
            auto it = kw.find(ident);
            if (it != kw.end()) {
                return Token{it->second, std::move(ident), startLine};
            }
            return Token{TokenKind::NAME, std::move(ident), startLine};
        }

        // Number
        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            std::string text;
            bool isFloat = false;
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                text.push_back(peek());
                advance();
            }
            if (peek() == '.') {
                isFloat = true;
                text.push_back(peek());
                advance();
                while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    text.push_back(peek());
                    advance();
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                isFloat = true;
                text.push_back(peek());
                advance();
                if (peek() == '+' || peek() == '-') {
                    text.push_back(peek());
                    advance();
                }
                while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    text.push_back(peek());
                    advance();
                }
            }
            return Token{
                isFloat ? TokenKind::NUMBER_FLOAT : TokenKind::NUMBER_INT,
                std::move(text),
                startLine
            };
        }

        // String literal — single or double quoted, no escapes beyond \\ \" \' \n \t
        if (c == '"' || c == '\'') {
            const char quote = c;
            advance();
            std::string text;
            while (!eof() && peek() != quote) {
                if (peek() == '\\') {
                    advance();
                    char esc = peek();
                    advance();
                    switch (esc) {
                        case '\\': text.push_back('\\'); break;
                        case '"':  text.push_back('"');  break;
                        case '\'': text.push_back('\''); break;
                        case 'n':  text.push_back('\n'); break;
                        case 't':  text.push_back('\t'); break;
                        case 'r':  text.push_back('\r'); break;
                        case '0':  text.push_back('\0'); break;
                        default:
                            fail(file_, line_,
                                 std::string{"unknown string escape '\\"} + esc + "'");
                    }
                } else if (peek() == '\n') {
                    fail(file_, line_, "newline inside string literal (unterminated string)");
                } else {
                    text.push_back(peek());
                    advance();
                }
            }
            if (eof()) fail(file_, line_, "unterminated string literal");
            advance(); // closing quote
            return Token{TokenKind::STRING, std::move(text), startLine};
        }

        // Operators / punctuation
        auto twoChar = [&](char a, char b, TokenKind kind) -> bool {
            if (peek() == a && peek(1) == b) {
                advance();
                advance();
                return true;
            }
            return false;
        };

        if (twoChar('=', '=', TokenKind::EQ)) return Token{TokenKind::EQ, "==", startLine};
        if (twoChar('~', '=', TokenKind::NEQ)) return Token{TokenKind::NEQ, "~=", startLine};
        if (twoChar('<', '=', TokenKind::LE)) return Token{TokenKind::LE, "<=", startLine};
        if (twoChar('>', '=', TokenKind::GE)) return Token{TokenKind::GE, ">=", startLine};
        if (peek() == '.' && peek(1) == '.' && peek(2) == '.') {
            advance(); advance(); advance();
            return Token{TokenKind::ELLIPSIS, "...", startLine};
        }

        const char one = peek();
        advance();
        switch (one) {
            case '+': return Token{TokenKind::PLUS, "+", startLine};
            case '-': return Token{TokenKind::MINUS, "-", startLine};
            case '*': return Token{TokenKind::STAR, "*", startLine};
            case '/': return Token{TokenKind::SLASH, "/", startLine};
            case '%': return Token{TokenKind::PERCENT, "%", startLine};
            case '^': return Token{TokenKind::CARET, "^", startLine};
            case '<': return Token{TokenKind::LT, "<", startLine};
            case '>': return Token{TokenKind::GT, ">", startLine};
            case '=': return Token{TokenKind::ASSIGN, "=", startLine};
            case '(': return Token{TokenKind::LPAREN, "(", startLine};
            case ')': return Token{TokenKind::RPAREN, ")", startLine};
            case '{': return Token{TokenKind::LBRACE, "{", startLine};
            case '}': return Token{TokenKind::RBRACE, "}", startLine};
            case '[': return Token{TokenKind::LBRACKET, "[", startLine};
            case ']': return Token{TokenKind::RBRACKET, "]", startLine};
            case ',': return Token{TokenKind::COMMA, ",", startLine};
            case ';': return Token{TokenKind::SEMI, ";", startLine};
            case ':': return Token{TokenKind::COLON, ":", startLine};
            case '.': return Token{TokenKind::DOT, ".", startLine};
            case '#': return Token{TokenKind::HASH, "#", startLine};
            default:
                fail(file_, startLine,
                     std::string{"unexpected character '"} + one +
                         "' (CODEGEN bodies do not support this syntax)");
        }
    }
};

std::vector<Token> tokenize(
    const std::string &src,
    const std::string &file,
    int firstLineInFile
) {
    Lexer lex(src, file, firstLineInFile);
    std::vector<Token> tokens;
    while (true) {
        Token t = lex.nextToken();
        const bool isEnd = t.kind_ == TokenKind::END_OF_INPUT;
        tokens.push_back(std::move(t));
        if (isEnd) break;
    }
    return tokens;
}

// ---- Parser ----------------------------------------------------------------
//
// Recursive-descent. Expression operator precedence is handled by precedence
// climbing. Lua's actual precedence table is mostly used; the DSL doesn't
// support `..` (concatenation), `^` (we accept the token but reject it at
// emission time as out-of-DSL), or `not` chains beyond the unary level.

struct Parser {
    const std::vector<Token> &tokens_;
    const std::string &file_;
    size_t pos_ = 0;

    Parser(const std::vector<Token> &tokens, const std::string &file)
        : tokens_(tokens), file_(file) {}

    const Token &cur() const { return tokens_[pos_]; }
    const Token &lookahead(size_t n) const {
        return tokens_[(pos_ + n < tokens_.size()) ? pos_ + n : tokens_.size() - 1];
    }
    bool check(TokenKind k) const { return cur().kind_ == k; }
    bool consumeIf(TokenKind k) {
        if (check(k)) { ++pos_; return true; }
        return false;
    }
    const Token &expect(TokenKind k, const char *what) {
        if (!check(k)) {
            fail(file_, cur().line_,
                 std::string{"expected "} + what + ", got '" + cur().text_ + "'");
        }
        return tokens_[pos_++];
    }

    // Body := stmt* (no return at v1's top level — bodies tick, not return)
    std::vector<StmtPtr> parseBlock(TokenKind terminator) {
        std::vector<StmtPtr> stmts;
        while (!check(terminator) && !check(TokenKind::END_OF_INPUT)) {
            // Permit Lua-style trailing semicolons.
            if (consumeIf(TokenKind::SEMI)) continue;
            stmts.push_back(parseStmt());
        }
        return stmts;
    }
    std::vector<StmtPtr> parseBlockUntilAny(std::initializer_list<TokenKind> terminators) {
        std::vector<StmtPtr> stmts;
        while (!check(TokenKind::END_OF_INPUT)) {
            for (auto t : terminators) if (check(t)) return stmts;
            if (consumeIf(TokenKind::SEMI)) continue;
            stmts.push_back(parseStmt());
        }
        return stmts;
    }

    StmtPtr parseStmt() {
        const Token &t = cur();
        switch (t.kind_) {
            case TokenKind::KW_LOCAL:    return parseLocal();
            case TokenKind::KW_IF:       return parseIf();
            case TokenKind::KW_FOR:      return parseFor();
            case TokenKind::KW_RETURN:   return parseReturn();
            case TokenKind::KW_WHILE:
                fail(file_, t.line_,
                     "`while` loops are not supported in CODEGEN bodies (file a follow-up if a real demo needs them; "
                     "or mark the system mode='eval')");
            case TokenKind::KW_REPEAT:
                fail(file_, t.line_, "`repeat`/`until` loops are not supported in CODEGEN bodies");
            case TokenKind::KW_BREAK:
                fail(file_, t.line_, "`break` is not supported in CODEGEN bodies");
            case TokenKind::KW_GOTO:
                fail(file_, t.line_, "`goto` is not supported in CODEGEN bodies");
            case TokenKind::KW_FUNCTION:
                fail(file_, t.line_,
                     "nested function declarations are not supported in CODEGEN bodies "
                     "(the only function in a CODEGEN system body is the tick itself)");
            default:
                return parseAssignOrExprStmt();
        }
    }

    StmtPtr parseLocal() {
        const int line = cur().line_;
        expect(TokenKind::KW_LOCAL, "'local'");
        const Token &name = expect(TokenKind::NAME, "local variable name");
        if (consumeIf(TokenKind::COMMA)) {
            fail(file_, line,
                 "multiple-target `local a, b = ...` is not supported in CODEGEN bodies");
        }
        expect(TokenKind::ASSIGN,
               "'=' (CODEGEN locals must be initialized: `local x = expr`)");
        ExprPtr init = parseExpr();
        auto stmt = std::make_unique<Stmt>();
        stmt->kind_ = StmtKind::LOCAL_DECL;
        stmt->line_ = line;
        stmt->localName_ = name.text_;
        stmt->localInit_ = std::move(init);
        return stmt;
    }

    StmtPtr parseIf() {
        const int line = cur().line_;
        expect(TokenKind::KW_IF, "'if'");
        auto stmt = std::make_unique<Stmt>();
        stmt->kind_ = StmtKind::IF;
        stmt->line_ = line;

        IfBranch first;
        first.cond_ = parseExpr();
        expect(TokenKind::KW_THEN, "'then'");
        first.body_ = parseBlockUntilAny(
            {TokenKind::KW_ELSEIF, TokenKind::KW_ELSE, TokenKind::KW_END});
        stmt->ifBranches_.push_back(std::move(first));

        while (consumeIf(TokenKind::KW_ELSEIF)) {
            IfBranch br;
            br.cond_ = parseExpr();
            expect(TokenKind::KW_THEN, "'then'");
            br.body_ = parseBlockUntilAny(
                {TokenKind::KW_ELSEIF, TokenKind::KW_ELSE, TokenKind::KW_END});
            stmt->ifBranches_.push_back(std::move(br));
        }

        if (consumeIf(TokenKind::KW_ELSE)) {
            stmt->hasElse_ = true;
            stmt->elseBody_ = parseBlockUntilAny({TokenKind::KW_END});
        }

        expect(TokenKind::KW_END, "'end' to close 'if'");
        return stmt;
    }

    StmtPtr parseFor() {
        const int line = cur().line_;
        expect(TokenKind::KW_FOR, "'for'");
        const Token &name = expect(TokenKind::NAME, "for-loop variable name");
        if (check(TokenKind::COMMA) || check(TokenKind::KW_IN)) {
            fail(file_, line,
                 "only the canonical `for i = lo, hi do` form is supported in CODEGEN bodies "
                 "(no generic-for `pairs/ipairs`, no multi-variable numeric-for, no negative step)");
        }
        expect(TokenKind::ASSIGN, "'=' in for loop");
        ExprPtr lo = parseExpr();
        expect(TokenKind::COMMA, "',' between for-loop bounds");
        ExprPtr hi = parseExpr();
        if (consumeIf(TokenKind::COMMA)) {
            fail(file_, line,
                 "for-loop step is not supported in CODEGEN bodies "
                 "(canonical form is `for i = 0, arch.length - 1 do`)");
        }
        expect(TokenKind::KW_DO, "'do' after for bounds");
        auto body = parseBlockUntilAny({TokenKind::KW_END});
        expect(TokenKind::KW_END, "'end' to close 'for'");

        auto stmt = std::make_unique<Stmt>();
        stmt->kind_ = StmtKind::FOR_NUMERIC;
        stmt->line_ = line;
        stmt->forVar_ = name.text_;
        stmt->forLo_ = std::move(lo);
        stmt->forHi_ = std::move(hi);
        stmt->forBody_ = std::move(body);
        return stmt;
    }

    StmtPtr parseReturn() {
        const int line = cur().line_;
        expect(TokenKind::KW_RETURN, "'return'");
        auto stmt = std::make_unique<Stmt>();
        stmt->kind_ = StmtKind::RETURN;
        stmt->line_ = line;
        // Return with or without expression. Most CODEGEN bodies don't use return.
        if (!check(TokenKind::KW_END) &&
            !check(TokenKind::KW_ELSE) &&
            !check(TokenKind::KW_ELSEIF) &&
            !check(TokenKind::SEMI) &&
            !check(TokenKind::END_OF_INPUT)) {
            stmt->returnExpr_ = parseExpr();
            stmt->hasReturnExpr_ = true;
        }
        return stmt;
    }

    // assign-or-exprstmt: a primary-expr followed by either `=` (assign) or
    // nothing (call statement). For column setters (arch.Comp:setAt(i, val) /
    // arch.Comp:setField(i, "f", val)), the call form is the canonical one
    // (Lua sugar — equivalent to `arch.Comp.setAt(arch.Comp, i, val)`); we
    // recognize that pattern as an ASSIGN with a column-write target.
    StmtPtr parseAssignOrExprStmt() {
        const int line = cur().line_;

        // Detect column-write method call: NAME '.' NAME ':' setAt|setField '(' ... ')'
        // or: NAME '.' NAME ':' at|getField — these are read calls inside expressions,
        // not statements; bare-method-call statements (other than setters) are rare and
        // we reject them as out-of-DSL.
        if (check(TokenKind::NAME) && lookahead(1).kind_ == TokenKind::DOT &&
            lookahead(2).kind_ == TokenKind::NAME && lookahead(3).kind_ == TokenKind::COLON &&
            lookahead(4).kind_ == TokenKind::NAME && lookahead(5).kind_ == TokenKind::LPAREN) {
            const std::string archName = cur().text_;
            const std::string compName = lookahead(2).text_;
            const std::string methodName = lookahead(4).text_;
            if (methodName == "setAt" || methodName == "setField") {
                if (archName != "arch") {
                    fail(file_, line,
                         "column-write must be on `arch` (the tick parameter): `arch.Comp:setAt(...)` / "
                         "`arch.Comp:setField(...)`");
                }
                pos_ += 5; // NAME . NAME : NAME
                expect(TokenKind::LPAREN, "'('");
                ExprPtr indexExpr = parseExpr();
                std::string fieldNameLit;
                if (methodName == "setField") {
                    expect(TokenKind::COMMA, "',' (after row index in setField)");
                    if (!check(TokenKind::STRING)) {
                        fail(file_, cur().line_,
                             "setField requires a string-literal field name (CODEGEN does not support "
                             "dynamic field names)");
                    }
                    fieldNameLit = cur().text_;
                    ++pos_;
                }
                expect(TokenKind::COMMA, "',' (before value in column setter)");
                ExprPtr valueExpr = parseExpr();
                expect(TokenKind::RPAREN, "')' to close column-setter call");

                auto stmt = std::make_unique<Stmt>();
                stmt->kind_ = StmtKind::ASSIGN;
                stmt->line_ = line;
                stmt->assignTarget_.kind_ = (methodName == "setAt")
                                                ? AssignTargetKind::COLUMN_SET_AT
                                                : AssignTargetKind::COLUMN_SET_FIELD;
                stmt->assignTarget_.componentName_ = compName;
                stmt->assignTarget_.fieldNameLiteral_ = fieldNameLit;
                stmt->assignTarget_.indexExpr_ = std::move(indexExpr);
                stmt->assignTarget_.line_ = line;
                stmt->assignRhs_ = std::move(valueExpr);
                return stmt;
            }
        }

        // Otherwise it's a NAME '=' expr  assignment.
        if (check(TokenKind::NAME) && lookahead(1).kind_ == TokenKind::ASSIGN) {
            const std::string targetName = cur().text_;
            pos_ += 1;
            expect(TokenKind::ASSIGN, "'='");
            ExprPtr rhs = parseExpr();
            auto stmt = std::make_unique<Stmt>();
            stmt->kind_ = StmtKind::ASSIGN;
            stmt->line_ = line;
            stmt->assignTarget_.kind_ = AssignTargetKind::NAME;
            stmt->assignTarget_.name_ = targetName;
            stmt->assignTarget_.line_ = line;
            stmt->assignRhs_ = std::move(rhs);
            return stmt;
        }

        // Anything else: parse the expression. It's accepted only as a bare
        // call to a whitelisted side-effecting binding (#1616); otherwise
        // reject it. Either:
        //   - `var.field = ...` (unsupported assignment target — point at the
        //     actual supported targets), or
        //   - a bare INTRINSIC_CALL like `IRRender.setSunIntensity(x)` → an
        //     expression statement (emission validates it's a statement-allowed
        //     void binding; a value-returning one like `math.sin(0)` is rejected
        //     there), or
        //   - any other bare expression like `pos.x` (no side effect; a typo).
        ExprPtr e = parseExpr();
        if (check(TokenKind::ASSIGN)) {
            fail(file_, line,
                 "the only supported assignment targets in CODEGEN bodies are "
                 "bare locals (`x = expr`), column rows (`arch.Comp:setAt(i, val)`), "
                 "and column fields (`arch.Comp:setField(i, \"f\", val)`)");
        }
        if (e->kind_ == ExprKind::INTRINSIC_CALL) {
            auto stmt = std::make_unique<Stmt>();
            stmt->kind_ = StmtKind::EXPR_STMT;
            stmt->line_ = line;
            stmt->exprStmt_ = std::move(e);
            return stmt;
        }
        fail(file_, line,
             "bare expression statements are not supported in CODEGEN bodies; valid "
             "statements are `local`, assignment, `if`, `for`, column-write calls, and "
             "whitelisted side-effecting engine bindings (e.g. `IRRender.setSunIntensity(x)`)");
    }

    // Precedence climbing for binary expressions. Lower number = lower precedence.
    static int binOpPrecedence(TokenKind k) {
        switch (k) {
            case TokenKind::KW_OR:                                  return 1;
            case TokenKind::KW_AND:                                 return 2;
            case TokenKind::LT: case TokenKind::GT:
            case TokenKind::LE: case TokenKind::GE:
            case TokenKind::EQ: case TokenKind::NEQ:                return 3;
            case TokenKind::PLUS: case TokenKind::MINUS:            return 4;
            case TokenKind::STAR: case TokenKind::SLASH:
            case TokenKind::PERCENT:                                return 5;
            default: return -1;
        }
    }
    static BinOp tokenToBinOp(TokenKind k) {
        switch (k) {
            case TokenKind::PLUS:    return BinOp::ADD;
            case TokenKind::MINUS:   return BinOp::SUB;
            case TokenKind::STAR:    return BinOp::MUL;
            case TokenKind::SLASH:   return BinOp::DIV;
            case TokenKind::PERCENT: return BinOp::MOD;
            case TokenKind::LT:      return BinOp::LT;
            case TokenKind::GT:      return BinOp::GT;
            case TokenKind::LE:      return BinOp::LE;
            case TokenKind::GE:      return BinOp::GE;
            case TokenKind::EQ:      return BinOp::EQ;
            case TokenKind::NEQ:     return BinOp::NEQ;
            case TokenKind::KW_AND:  return BinOp::AND;
            case TokenKind::KW_OR:   return BinOp::OR;
            default: throw std::logic_error("tokenToBinOp on non-binary token");
        }
    }

    ExprPtr parseExpr() { return parseBinaryExpr(0); }

    ExprPtr parseBinaryExpr(int minPrec) {
        ExprPtr lhs = parseUnaryExpr();
        while (true) {
            const int prec = binOpPrecedence(cur().kind_);
            if (prec < 0 || prec < minPrec) break;
            const TokenKind op = cur().kind_;
            const int line = cur().line_;
            ++pos_;
            ExprPtr rhs = parseBinaryExpr(prec + 1);
            auto bin = std::make_unique<Expr>();
            bin->kind_ = ExprKind::BINARY_OP;
            bin->line_ = line;
            bin->binOp_ = tokenToBinOp(op);
            bin->lhs_ = std::move(lhs);
            bin->rhs_ = std::move(rhs);
            lhs = std::move(bin);
        }
        return lhs;
    }

    ExprPtr parseUnaryExpr() {
        if (check(TokenKind::MINUS) || check(TokenKind::KW_NOT)) {
            const TokenKind tk = cur().kind_;
            const int line = cur().line_;
            ++pos_;
            auto inner = parseUnaryExpr();
            auto un = std::make_unique<Expr>();
            un->kind_ = ExprKind::UNARY_OP;
            un->line_ = line;
            un->unOp_ = (tk == TokenKind::MINUS) ? UnOp::NEG : UnOp::NOT;
            un->lhs_ = std::move(inner);
            return un;
        }
        if (check(TokenKind::HASH)) {
            fail(file_, cur().line_,
                 "the `#` length operator is not supported in CODEGEN bodies "
                 "(use `arch.length` for archetype size)");
        }
        if (check(TokenKind::CARET)) {
            fail(file_, cur().line_,
                 "the `^` exponent operator is not in the CODEGEN intrinsic set "
                 "(use a math.* intrinsic)");
        }
        return parsePostfixExpr();
    }

    // primary := NAME | NUMBER | STRING | BOOL | '(' expr ')' | call-chain
    // call-chain := primary ( '.' NAME | ':' NAME args | args )*
    ExprPtr parsePostfixExpr() {
        ExprPtr base = parsePrimaryExpr();
        while (true) {
            if (check(TokenKind::DOT)) {
                const int line = cur().line_;
                ++pos_;
                const Token &fieldTok = expect(TokenKind::NAME, "field name after '.'");
                // Special case: `arch.Comp.new(args)` -> the RHS form `Comp.new(...)`
                // is detected at primary-level; here we may see `Comp.new(...)` after
                // a NAME_REF base. Leave as INDEX by default; emission decides.
                if (base->kind_ == ExprKind::NAME_REF && fieldTok.text_ == "new" &&
                    check(TokenKind::LPAREN)) {
                    // Component.new(args...) constructor call.
                    const std::string compName = base->name_;
                    ++pos_; // consume '('
                    auto call = std::make_unique<Expr>();
                    call->kind_ = ExprKind::COMPONENT_NEW;
                    call->line_ = line;
                    call->componentName_ = compName;
                    if (!check(TokenKind::RPAREN)) {
                        call->args_.push_back(parseExpr());
                        while (consumeIf(TokenKind::COMMA)) call->args_.push_back(parseExpr());
                    }
                    expect(TokenKind::RPAREN, "')' to close Comp.new(...) call");
                    base = std::move(call);
                    continue;
                }
                // Special case: namespace.func(args...) intrinsic call
                if (base->kind_ == ExprKind::NAME_REF && check(TokenKind::LPAREN)) {
                    const std::string ns = base->name_;
                    const std::string fname = fieldTok.text_;
                    ++pos_; // consume '('
                    auto call = std::make_unique<Expr>();
                    call->kind_ = ExprKind::INTRINSIC_CALL;
                    call->line_ = line;
                    call->intrinsicNamespace_ = ns;
                    call->intrinsicName_ = fname;
                    if (!check(TokenKind::RPAREN)) {
                        call->args_.push_back(parseExpr());
                        while (consumeIf(TokenKind::COMMA)) call->args_.push_back(parseExpr());
                    }
                    expect(TokenKind::RPAREN, "')' to close intrinsic call");
                    base = std::move(call);
                    continue;
                }
                // Plain field index — e.g. `pos.x`, `arch.length`, `vel.y`
                auto idx = std::make_unique<Expr>();
                idx->kind_ = ExprKind::INDEX;
                idx->line_ = line;
                idx->receiver_ = std::move(base);
                idx->field_ = fieldTok.text_;
                base = std::move(idx);
                continue;
            }
            if (check(TokenKind::COLON)) {
                // method call: `arch.Comp:at(i)` or `arch.Comp:getField(i, "name")`
                // The base must be `arch.<CompName>` (an INDEX).
                const int line = cur().line_;
                ++pos_;
                const Token &methodTok = expect(TokenKind::NAME, "method name after ':'");
                expect(TokenKind::LPAREN, "'(' after method name");

                if (base->kind_ != ExprKind::INDEX || base->receiver_->kind_ != ExprKind::NAME_REF ||
                    base->receiver_->name_ != "arch") {
                    fail(file_, line,
                         "method calls (`x:y(...)`) are only supported on `arch.<Component>` in CODEGEN bodies");
                }
                const std::string compName = base->field_;

                if (methodTok.text_ == "at") {
                    auto call = std::make_unique<Expr>();
                    call->kind_ = ExprKind::COLUMN_AT;
                    call->line_ = line;
                    call->componentName_ = compName;
                    call->args_.push_back(parseExpr());
                    expect(TokenKind::RPAREN, "')' to close arch.Comp:at(i)");
                    base = std::move(call);
                    continue;
                }
                if (methodTok.text_ == "getField") {
                    auto call = std::make_unique<Expr>();
                    call->kind_ = ExprKind::COLUMN_GET_FIELD;
                    call->line_ = line;
                    call->componentName_ = compName;
                    call->args_.push_back(parseExpr());
                    expect(TokenKind::COMMA, "',' (between row index and field name in getField)");
                    if (!check(TokenKind::STRING)) {
                        fail(file_, cur().line_,
                             "getField requires a string-literal field name (CODEGEN does not support "
                             "dynamic field names)");
                    }
                    call->fieldNameLiteral_ = cur().text_;
                    ++pos_;
                    expect(TokenKind::RPAREN, "')' to close arch.Comp:getField(i, \"...\")");
                    base = std::move(call);
                    continue;
                }
                fail(file_, line,
                     std::string{"unknown column op `:"} + methodTok.text_ + "` "
                     "(supported: `at`, `setAt`, `getField`, `setField`)");
            }
            if (check(TokenKind::LPAREN)) {
                fail(file_, cur().line_,
                     "bare function calls are not supported in CODEGEN bodies "
                     "(use a whitelisted intrinsic via `math.*` / `IRMath.*`, or a `Component.new(...)` constructor)");
            }
            if (check(TokenKind::LBRACKET)) {
                fail(file_, cur().line_,
                     "table indexing `[...]` is not supported in CODEGEN bodies "
                     "(use named field access)");
            }
            break;
        }
        return base;
    }

    ExprPtr parsePrimaryExpr() {
        const Token &t = cur();
        const int line = t.line_;
        switch (t.kind_) {
            case TokenKind::NUMBER_INT: {
                ++pos_;
                auto e = std::make_unique<Expr>();
                e->kind_ = ExprKind::NUMBER_LITERAL;
                e->isInt_ = true;
                try {
                    e->intValue_ = std::stoll(t.text_);
                } catch (const std::exception &) {
                    fail(file_, line, "integer literal '" + t.text_ + "' out of range");
                }
                e->line_ = line;
                return e;
            }
            case TokenKind::NUMBER_FLOAT: {
                ++pos_;
                auto e = std::make_unique<Expr>();
                e->kind_ = ExprKind::NUMBER_LITERAL;
                e->isInt_ = false;
                e->floatValue_ = std::stod(t.text_);
                e->line_ = line;
                return e;
            }
            case TokenKind::STRING: {
                ++pos_;
                auto e = std::make_unique<Expr>();
                e->kind_ = ExprKind::STRING_LITERAL;
                e->stringValue_ = t.text_;
                e->line_ = line;
                return e;
            }
            case TokenKind::KW_TRUE: case TokenKind::KW_FALSE: {
                ++pos_;
                auto e = std::make_unique<Expr>();
                e->kind_ = ExprKind::BOOL_LITERAL;
                e->boolValue_ = (t.kind_ == TokenKind::KW_TRUE);
                e->line_ = line;
                return e;
            }
            case TokenKind::KW_NIL: {
                fail(file_, line,
                     "`nil` is not supported in CODEGEN bodies "
                     "(CODEGEN values are always typed; use 0, 0.0, or false as appropriate)");
            }
            case TokenKind::NAME: {
                ++pos_;
                auto e = std::make_unique<Expr>();
                e->kind_ = ExprKind::NAME_REF;
                e->name_ = t.text_;
                e->line_ = line;
                return e;
            }
            case TokenKind::LPAREN: {
                ++pos_;
                ExprPtr inner = parseExpr();
                expect(TokenKind::RPAREN, "')'");
                return inner;
            }
            case TokenKind::LBRACE: {
                fail(file_, line,
                     "table constructors `{...}` are not supported in CODEGEN bodies "
                     "(use `Component.new(...)` for component values)");
            }
            case TokenKind::ELLIPSIS: {
                fail(file_, line, "varargs `...` are not supported in CODEGEN bodies");
            }
            default:
                fail(file_, line,
                     std::string{"unexpected '"} + t.text_ + "' at start of expression");
        }
    }
};

// ---- Type tracking + emission ----------------------------------------------
//
// To translate `pos.x` (Lua) into `pos.x_` (C++) reliably, the emitter must
// know the type of `pos`. This is a small bit of type inference:
//   - `local hp = arch.Hp:at(i)`  →  hp has type C_Hp
//   - `local cur = arch.Hp:getField(i, "current")`  →  cur has type field-type
//   - `local x = pos.x`  →  x has the field type of Hp::x_
//   - `local y = a + b`  →  y inherits type by op-rule (mixed → float)
// The DSL is small enough that a single forward pass with a name → type
// dictionary suffices.

enum class ExprType {
    UNKNOWN,
    INT32,
    FLOAT,
    BOOL,
    STRING,
    COMPONENT,        // a Lua-defined codegen'd component value (typed by name)
    VEC3,             // IRMath::vec3 — from a vec3 field or `vec3.new(x,y,z)`
    IVEC3,            // IRMath::ivec3 — from an ivec3 field or `ivec3.new(x,y,z)`
};

struct Symbol {
    ExprType type_ = ExprType::UNKNOWN;
    std::string componentName_;   // when type_ == COMPONENT
};

ExprType fieldTypeToExprType(FieldType t) {
    switch (t) {
        case FieldType::INT32:  return ExprType::INT32;
        case FieldType::FLOAT:  return ExprType::FLOAT;
        case FieldType::BOOL:   return ExprType::BOOL;
        case FieldType::STRING: return ExprType::STRING;
        case FieldType::VEC3:   return ExprType::VEC3;
        case FieldType::IVEC3:  return ExprType::IVEC3;
    }
    return ExprType::UNKNOWN;
}

const char *cppTypeForFieldType(FieldType t) {
    switch (t) {
        case FieldType::INT32:  return "std::int32_t";
        case FieldType::FLOAT:  return "float";
        case FieldType::BOOL:   return "bool";
        case FieldType::STRING: return "std::string";
        case FieldType::VEC3:   return "IRMath::vec3";
        case FieldType::IVEC3:  return "IRMath::ivec3";
    }
    return "/*unknown*/";
}

const char *cppTypeForExprType(ExprType t) {
    switch (t) {
        case ExprType::INT32:    return "std::int32_t";
        case ExprType::FLOAT:    return "float";
        case ExprType::BOOL:     return "bool";
        case ExprType::STRING:   return "std::string";
        case ExprType::VEC3:     return "IRMath::vec3";
        case ExprType::IVEC3:    return "IRMath::ivec3";
        default:                 return "auto";
    }
}

// ---- #1353: row-alias vs row-copy analysis ---------------------------------
//
// The per-row emitter lowers `local a = arch.C:at(i)` to `auto a =
// _ir_row_C;` — a by-value copy of the whole component row. For read-light
// kernels that copy is the dominant per-row cost (#1353: ~2.5x hand-C++ at
// 1024 rows). When the binding is only ever READ, an alias (`const auto& a =
// _ir_row_C;`) is observationally identical and skips the copy.
//
// Soundness rests on one fact about the DSL: a component-typed local cannot
// be mutated through field assignment — `a.x = ...` is rejected by the
// parser, so the only way component C's row changes mid-body is a
// `setAt`/`setField` on `arch.C`. A copy freezes the row at bind time; an
// alias tracks live writes. They diverge exactly when a read of `a` is
// sequenced AFTER a write to C's row. The analysis below emits an alias only
// when the binding is never reassigned and no read of it can follow a write
// to its component; any uncertainty falls back to the safe by-value copy.

// True if `e` reads the local `name` anywhere in the expression tree.
bool exprReadsName(const Expr &e, const std::string &name) {
    switch (e.kind_) {
        case ExprKind::NAME_REF:
            return e.name_ == name;
        case ExprKind::UNARY_OP:
            return e.lhs_ && exprReadsName(*e.lhs_, name);
        case ExprKind::BINARY_OP:
            return (e.lhs_ && exprReadsName(*e.lhs_, name)) ||
                   (e.rhs_ && exprReadsName(*e.rhs_, name));
        case ExprKind::INDEX:   // `a.field` — receiver is the NAME_REF
            return e.receiver_ && exprReadsName(*e.receiver_, name);
        case ExprKind::INTRINSIC_CALL:
        case ExprKind::COLUMN_AT:
        case ExprKind::COLUMN_GET_FIELD:
        case ExprKind::COMPONENT_NEW:
            for (const auto &a : e.args_) if (a && exprReadsName(*a, name)) return true;
            return false;
        default:                // literals
            return false;
    }
}

bool blockWritesComponent(const std::vector<StmtPtr> &stmts, const std::string &comp);

// True if statement `s` (recursing into nested if/for bodies) writes
// component `comp` via `arch.comp:setAt`/`setField`.
bool stmtWritesComponent(const Stmt &s, const std::string &comp) {
    switch (s.kind_) {
        case StmtKind::ASSIGN:
            return (s.assignTarget_.kind_ == AssignTargetKind::COLUMN_SET_AT ||
                    s.assignTarget_.kind_ == AssignTargetKind::COLUMN_SET_FIELD) &&
                   s.assignTarget_.componentName_ == comp;
        case StmtKind::IF: {
            for (const auto &br : s.ifBranches_)
                if (blockWritesComponent(br.body_, comp)) return true;
            return s.hasElse_ && blockWritesComponent(s.elseBody_, comp);
        }
        case StmtKind::FOR_NUMERIC:
            return blockWritesComponent(s.forBody_, comp);
        default:
            return false;
    }
}

bool blockWritesComponent(const std::vector<StmtPtr> &stmts, const std::string &comp) {
    for (const auto &s : stmts) if (s && stmtWritesComponent(*s, comp)) return true;
    return false;
}

// Forward pass over `stmts[startIdx..]` for the binding `name` (component
// `comp`). `cWritten` is true if `comp` may already have been written before
// this range. Sets `unsafe = true` if `name` is reassigned, redeclared
// (shadowed), or read after a write to `comp`. A nested loop is entered with
// `cWritten` OR-ed with whether its body writes `comp`, so a write-then-read
// across the loop back-edge is caught; the enclosing loop needs no such
// treatment because the binding's decl re-runs (refreshing a copy) at the
// top of every iteration.
void analyzeForward(const std::vector<StmtPtr> &stmts, std::size_t startIdx,
                    const std::string &name, const std::string &comp,
                    bool cWritten, bool &unsafe) {
    for (std::size_t i = startIdx; i < stmts.size(); ++i) {
        if (!stmts[i]) continue;
        const Stmt &s = *stmts[i];
        switch (s.kind_) {
            case StmtKind::LOCAL_DECL:
                if (s.localInit_ && exprReadsName(*s.localInit_, name) && cWritten)
                    unsafe = true;
                if (s.localName_ == name) unsafe = true;   // shadow → fall back to copy
                break;
            case StmtKind::ASSIGN: {
                // Index + RHS are evaluated before the column write takes effect.
                if (s.assignTarget_.indexExpr_ &&
                    exprReadsName(*s.assignTarget_.indexExpr_, name) && cWritten)
                    unsafe = true;
                if (s.assignRhs_ && exprReadsName(*s.assignRhs_, name) && cWritten)
                    unsafe = true;
                if (s.assignTarget_.kind_ == AssignTargetKind::NAME &&
                    s.assignTarget_.name_ == name)
                    unsafe = true;                          // reassignment
                if ((s.assignTarget_.kind_ == AssignTargetKind::COLUMN_SET_AT ||
                     s.assignTarget_.kind_ == AssignTargetKind::COLUMN_SET_FIELD) &&
                    s.assignTarget_.componentName_ == comp)
                    cWritten = true;
                break;
            }
            case StmtKind::IF:
                for (const auto &br : s.ifBranches_) {
                    if (br.cond_ && exprReadsName(*br.cond_, name) && cWritten)
                        unsafe = true;
                    analyzeForward(br.body_, 0, name, comp, cWritten, unsafe);
                }
                if (s.hasElse_)
                    analyzeForward(s.elseBody_, 0, name, comp, cWritten, unsafe);
                if (stmtWritesComponent(s, comp)) cWritten = true;
                break;
            case StmtKind::FOR_NUMERIC: {
                if (s.forLo_ && exprReadsName(*s.forLo_, name) && cWritten) unsafe = true;
                if (s.forHi_ && exprReadsName(*s.forHi_, name) && cWritten) unsafe = true;
                const bool bodyWrites = blockWritesComponent(s.forBody_, comp);
                analyzeForward(s.forBody_, 0, name, comp, cWritten || bodyWrites, unsafe);
                if (bodyWrites) cWritten = true;
                break;
            }
            case StmtKind::RETURN:
                if (s.hasReturnExpr_ && s.returnExpr_ &&
                    exprReadsName(*s.returnExpr_, name) && cWritten)
                    unsafe = true;
                break;
            case StmtKind::EXPR_STMT:
                if (s.exprStmt_ && exprReadsName(*s.exprStmt_, name) && cWritten)
                    unsafe = true;
                break;
        }
    }
}

// Walk `stmts`, adding every `local a = arch.C:at(i)` decl whose binding is
// alias-safe to `out` (keyed by the LOCAL_DECL Stmt address). Recurses into
// nested if/for bodies so decls in inner scopes are analysed in their own
// scope.
void collectRefSafeColumnAtDecls(const std::vector<StmtPtr> &stmts,
                                 std::unordered_set<const Stmt *> &out) {
    for (std::size_t k = 0; k < stmts.size(); ++k) {
        if (!stmts[k]) continue;
        const Stmt &s = *stmts[k];
        if (s.kind_ == StmtKind::LOCAL_DECL && s.localInit_ &&
            s.localInit_->kind_ == ExprKind::COLUMN_AT) {
            bool unsafe = false;
            analyzeForward(stmts, k + 1, s.localName_,
                           s.localInit_->componentName_, /*cWritten=*/false, unsafe);
            if (!unsafe) out.insert(&s);
        }
        switch (s.kind_) {
            case StmtKind::IF:
                for (const auto &br : s.ifBranches_) collectRefSafeColumnAtDecls(br.body_, out);
                if (s.hasElse_) collectRefSafeColumnAtDecls(s.elseBody_, out);
                break;
            case StmtKind::FOR_NUMERIC:
                collectRefSafeColumnAtDecls(s.forBody_, out);
                break;
            default:
                break;
        }
    }
}

struct Emitter {
    std::ostringstream out_;
    int indent_ = 0;
    const std::string &file_;
    const std::vector<ComponentSchema> &registry_;
    std::unordered_map<std::string, Symbol> symbols_;

    // #1353: LOCAL_DECL Stmt addresses whose `arch.C:at(i)` binding is safe to
    // emit as a `const auto&` alias of the row instead of a by-value copy.
    // Populated by collectRefSafeColumnAtDecls() before emission.
    std::unordered_set<const Stmt *> refSafeColumnAtDecls_;

    // Per-row emission mode. When non-empty, the emitted lambda is
    // the per-component form `[](C_X& _ir_row_X, ...)` rather than the
    // batch form `[](Archetype&, vector<EntityId>&, vector<C_X>&, ...)`,
    // and column-op index expressions that match `loopVarName_` lower to
    // the per-row reference `_ir_row_X` (or `_ir_row_X.field_`) instead
    // of `_ir_col_X[i]`. Indexing with any other expression is a parse
    // error — per-row form has no column vector to scatter-index into.
    std::string loopVarName_;

    // #1616: engine-binding headers required by whitelisted side-effecting
    // intrinsics emitted in this body (e.g. ir_render.hpp for IRRender.*).
    // Drained by emitSystem into the generated header's #include block.
    std::set<std::string> requiredIncludes_;

    Emitter(const std::string &file, const std::vector<ComponentSchema> &registry)
        : file_(file), registry_(registry) {}

    void writeIndent() { for (int i = 0; i < indent_; ++i) out_ << "    "; }

    // #1616: resolve an INTRINSIC_CALL against the whitelist + arity. Fails
    // (codegen error) on an unknown name or arity mismatch. Shared by
    // expression-position (emitExpr) and statement-position (EXPR_STMT)
    // lowering so the two paths agree on the diagnostic.
    const Intrinsic &resolveIntrinsic(const Expr &e) const {
        const Intrinsic *intr = findIntrinsic(e.intrinsicNamespace_, e.intrinsicName_);
        if (!intr) {
            fail(file_, e.line_,
                 "intrinsic `" + e.intrinsicNamespace_ + "." + e.intrinsicName_ +
                     "` is not in the CODEGEN whitelist (see cmake/lua_codegen/system_dsl.cpp)");
        }
        if (intr->arity_ >= 0 && static_cast<int>(e.args_.size()) != intr->arity_) {
            fail(file_, e.line_,
                 "intrinsic `" + e.intrinsicNamespace_ + "." + e.intrinsicName_ +
                     "` expects " + std::to_string(intr->arity_) + " argument(s), got " +
                     std::to_string(e.args_.size()));
        }
        return *intr;
    }

    // #1616: emit `cppExpression_(arg0, arg1, ...)` with no trailing
    // punctuation. Shared by both intrinsic-call positions.
    void emitIntrinsicCall(const Expr &e, const Intrinsic &intr) {
        out_ << intr.cppExpression_ << "(";
        for (size_t i = 0; i < e.args_.size(); ++i) {
            if (i) out_ << ", ";
            emitExpr(*e.args_[i]);
        }
        out_ << ")";
    }

    // True when this expression is exactly the per-row loop variable
    // reference (used to validate column-op index args in per-row mode).
    bool isLoopVarRef(const Expr &e) const {
        return !loopVarName_.empty() &&
               e.kind_ == ExprKind::NAME_REF &&
               e.name_ == loopVarName_;
    }

    const ComponentSchema *findComponent(const std::string &name) const {
        for (const auto &c : registry_) if (c.name_ == name) return &c;
        return nullptr;
    }

    const ComponentField *findField(const ComponentSchema &c, const std::string &fieldName) const {
        for (const auto &f : c.fields_) if (f.name_ == fieldName) return &f;
        return nullptr;
    }

    // Emit an expression. Returns its inferred type for caller chaining.
    ExprType emitExpr(const Expr &e) {
        switch (e.kind_) {
            case ExprKind::NUMBER_LITERAL:
                if (e.isInt_) {
                    out_ << "static_cast<std::int32_t>(" << e.intValue_ << ")";
                    return ExprType::INT32;
                }
                {
                    // Match T-106's float-literal rendering: ensure a decimal
                    // point is present before the `f` suffix (`0f` is a parse
                    // error; `0.0f` is fine).
                    std::ostringstream tmp;
                    tmp.precision(9);
                    tmp << e.floatValue_;
                    std::string s = tmp.str();
                    if (s.find('.') == std::string::npos &&
                        s.find('e') == std::string::npos &&
                        s.find('E') == std::string::npos) {
                        s += ".0";
                    }
                    s += "f";
                    out_ << s;
                }
                return ExprType::FLOAT;
            case ExprKind::BOOL_LITERAL:
                out_ << (e.boolValue_ ? "true" : "false");
                return ExprType::BOOL;
            case ExprKind::STRING_LITERAL: {
                out_ << "std::string{\"";
                for (char c : e.stringValue_) {
                    switch (c) {
                        case '\\': out_ << "\\\\"; break;
                        case '"':  out_ << "\\\""; break;
                        case '\n': out_ << "\\n"; break;
                        default:   out_ << c;
                    }
                }
                out_ << "\"}";
                return ExprType::STRING;
            }
            case ExprKind::NAME_REF: {
                // Special: `arch.length - 1` patterns parse `arch` here and `length`
                // through INDEX. Bare `arch` reference at expression level is an
                // error (we never want to pass arch around).
                if (e.name_ == "arch") {
                    fail(file_, e.line_,
                         "bare reference to `arch` is not supported in CODEGEN bodies "
                         "(use `arch.length` or `arch.Comp:...`)");
                }
                auto it = symbols_.find(e.name_);
                if (it == symbols_.end()) {
                    fail(file_, e.line_,
                         "unknown identifier `" + e.name_ +
                             "` (CODEGEN bodies only see `local`-declared names and the canonical loop variable)");
                }
                out_ << e.name_;
                return it->second.type_;
            }
            case ExprKind::UNARY_OP: {
                if (e.unOp_ == UnOp::NEG) {
                    out_ << "(-(";
                    auto t = emitExpr(*e.lhs_);
                    out_ << "))";
                    return t;
                }
                out_ << "(!(";
                emitExpr(*e.lhs_);
                out_ << "))";
                return ExprType::BOOL;
            }
            case ExprKind::BINARY_OP: {
                const char *op = nullptr;
                switch (e.binOp_) {
                    case BinOp::ADD: op = "+"; break;
                    case BinOp::SUB: op = "-"; break;
                    case BinOp::MUL: op = "*"; break;
                    case BinOp::DIV: op = "/"; break;
                    case BinOp::MOD: op = "%"; break;
                    case BinOp::LT:  op = "<"; break;
                    case BinOp::GT:  op = ">"; break;
                    case BinOp::LE:  op = "<="; break;
                    case BinOp::GE:  op = ">="; break;
                    case BinOp::EQ:  op = "=="; break;
                    case BinOp::NEQ: op = "!="; break;
                    case BinOp::AND: op = "&&"; break;
                    case BinOp::OR:  op = "||"; break;
                }
                out_ << "(";
                ExprType lt = emitExpr(*e.lhs_);
                if (lt == ExprType::VEC3 || lt == ExprType::IVEC3) {
                    fail(file_, e.line_,
                         "vector values have no whole-vector operators in CODEGEN; operate on "
                         "components via `.x` / `.y` / `.z`");
                }
                out_ << " " << op << " ";
                ExprType rt = emitExpr(*e.rhs_);
                out_ << ")";
                if (rt == ExprType::VEC3 || rt == ExprType::IVEC3) {
                    fail(file_, e.line_,
                         "vector values have no whole-vector operators in CODEGEN; operate on "
                         "components via `.x` / `.y` / `.z`");
                }
                // Result-type inference (kept simple for v1):
                switch (e.binOp_) {
                    case BinOp::LT: case BinOp::GT: case BinOp::LE: case BinOp::GE:
                    case BinOp::EQ: case BinOp::NEQ:
                    case BinOp::AND: case BinOp::OR:
                        return ExprType::BOOL;
                    case BinOp::MOD:
                        return (lt == ExprType::FLOAT || rt == ExprType::FLOAT) ? ExprType::FLOAT : ExprType::INT32;
                    default:
                        if (lt == ExprType::FLOAT || rt == ExprType::FLOAT) return ExprType::FLOAT;
                        if (lt == ExprType::INT32 && rt == ExprType::INT32) return ExprType::INT32;
                        return ExprType::FLOAT; // mixed/unknown numeric → float
                }
            }
            case ExprKind::INDEX: {
                // arch.length is a special case
                if (e.receiver_->kind_ == ExprKind::NAME_REF &&
                    e.receiver_->name_ == "arch" && e.field_ == "length") {
                    if (!loopVarName_.empty()) {
                        // Per-row form has no archetype column to size — the
                        // canonical `for i = 0, arch.length - 1` outer loop is dropped
                        // and the row count is implicit in the engine's per-row dispatch.
                        fail(file_, e.line_,
                             "`arch.length` is only valid as the upper bound of the "
                             "canonical `for i = 0, arch.length - 1 do` loop; CODEGEN "
                             "bodies have no archetype-length read outside that loop");
                    }
                    out_ << "static_cast<std::int32_t>(_ir_codegen_ids.size())";
                    return ExprType::INT32;
                }
                // arch.Comp:... goes through method-call branch — bare `arch.Comp` here
                // is a misuse.
                if (e.receiver_->kind_ == ExprKind::NAME_REF && e.receiver_->name_ == "arch") {
                    fail(file_, e.line_,
                         "`arch." + e.field_ + "` must be followed by a method call "
                         "(`:at(i)`, `:setAt(i, v)`, `:getField(i, \"f\")`, `:setField(i, \"f\", v)`)");
                }
                // var.field — must be a known component-typed local
                if (e.receiver_->kind_ != ExprKind::NAME_REF) {
                    fail(file_, e.line_,
                         "field access (`x.field`) is only supported on a `local` "
                         "of component type in CODEGEN bodies");
                }
                const std::string &recvName = e.receiver_->name_;
                auto it = symbols_.find(recvName);
                if (it == symbols_.end()) {
                    fail(file_, e.line_,
                         "field access on `" + recvName + "` requires it to be a `local` "
                         "declared in this body");
                }
                // vec3 / ivec3 value: only `.x` / `.y` / `.z` component reads.
                // IRMath::vec3 members are bare `.x/.y/.z` (no trailing `_`).
                if (it->second.type_ == ExprType::VEC3 || it->second.type_ == ExprType::IVEC3) {
                    if (e.field_ != "x" && e.field_ != "y" && e.field_ != "z") {
                        fail(file_, e.line_,
                             "vector value `" + recvName + "` supports only `.x`, `.y`, `.z` "
                             "(got `." + e.field_ + "`)");
                    }
                    out_ << recvName << "." << e.field_;
                    return it->second.type_ == ExprType::VEC3 ? ExprType::FLOAT : ExprType::INT32;
                }
                if (it->second.type_ != ExprType::COMPONENT) {
                    fail(file_, e.line_,
                         "field access on `" + recvName + "` requires it to hold a component or "
                         "vector value (declared via `local x = arch.Comp:at(i)`, `Comp.new(...)`, "
                         "or a vec3 / ivec3 field)");
                }
                const auto *schema = findComponent(it->second.componentName_);
                if (!schema) {
                    fail(file_, e.line_,
                         "internal: component `" + it->second.componentName_ + "` not in registry");
                }
                const auto *field = findField(*schema, e.field_);
                if (!field) {
                    fail(file_, e.line_,
                         "component `" + schema->name_ + "` has no field `" + e.field_ + "`");
                }
                out_ << recvName << "." << e.field_ << "_";
                return fieldTypeToExprType(field->type_);
            }
            case ExprKind::INTRINSIC_CALL: {
                const Intrinsic &intr = resolveIntrinsic(e);
                if (intr.isStatement_) {
                    // #1616: a void side-effecting binding has no value to use.
                    fail(file_, e.line_,
                         "binding `" + e.intrinsicNamespace_ + "." + e.intrinsicName_ +
                             "` returns void (side-effecting render-glue) and may only be used "
                             "as a bare statement, not inside an expression");
                }
                emitIntrinsicCall(e, intr);
                // Treat value-returning intrinsic results as float (the listed
                // set returns float/int; C++ template deduction handles the
                // rest where we feed the result into further float math).
                return ExprType::FLOAT;
            }
            case ExprKind::COLUMN_AT: {
                const auto *schema = findComponent(e.componentName_);
                if (!schema) {
                    fail(file_, e.line_,
                         "component `" + e.componentName_ +
                             "` is not declared via `IRComponent.register(...)` "
                             "(CODEGEN system bodies only support Lua-defined components)");
                }
                if (!loopVarName_.empty()) {
                    if (!isLoopVarRef(*e.args_[0])) {
                        fail(file_, e.line_,
                             "`arch." + e.componentName_ +
                                 ":at(...)` index must be the canonical loop variable `" +
                                 loopVarName_ + "` (CODEGEN bodies dispatch per-row; "
                                 "computed indexes have no per-row meaning)");
                    }
                    out_ << "_ir_row_" << e.componentName_;
                    return ExprType::COMPONENT;
                }
                out_ << "_ir_col_" << e.componentName_ << "[";
                emitExpr(*e.args_[0]);
                out_ << "]";
                return ExprType::COMPONENT;
            }
            case ExprKind::COLUMN_GET_FIELD: {
                const auto *schema = findComponent(e.componentName_);
                if (!schema) {
                    fail(file_, e.line_,
                         "component `" + e.componentName_ +
                             "` is not declared via `IRComponent.register(...)`");
                }
                const auto *field = findField(*schema, e.fieldNameLiteral_);
                if (!field) {
                    fail(file_, e.line_,
                         "component `" + schema->name_ + "` has no field `" + e.fieldNameLiteral_ + "`");
                }
                if (!loopVarName_.empty()) {
                    if (!isLoopVarRef(*e.args_[0])) {
                        fail(file_, e.line_,
                             "`arch." + e.componentName_ +
                                 ":getField(...)` index must be the canonical loop variable `" +
                                 loopVarName_ + "`");
                    }
                    out_ << "_ir_row_" << e.componentName_ << "." << e.fieldNameLiteral_ << "_";
                    return fieldTypeToExprType(field->type_);
                }
                out_ << "_ir_col_" << e.componentName_ << "[";
                emitExpr(*e.args_[0]);
                out_ << "]." << e.fieldNameLiteral_ << "_";
                return fieldTypeToExprType(field->type_);
            }
            case ExprKind::COMPONENT_NEW: {
                // Built-in vector constructors: `vec3.new(x, y, z)` /
                // `ivec3.new(x, y, z)`. These are not registered components —
                // they lower directly to IRMath aggregate literals so a tick
                // can write a packed field: `arch.C:setField(i, "pos", vec3.new(...))`.
                if (e.componentName_ == "vec3" || e.componentName_ == "ivec3") {
                    const bool isInt = e.componentName_ == "ivec3";
                    if (e.args_.size() != 3) {
                        fail(file_, e.line_,
                             "`" + e.componentName_ + ".new(...)` expects 3 arguments (x, y, z), got " +
                                 std::to_string(e.args_.size()));
                    }
                    out_ << (isInt ? "IRMath::ivec3{" : "IRMath::vec3{");
                    for (size_t i = 0; i < 3; ++i) {
                        if (i) out_ << ", ";
                        out_ << "static_cast<" << (isInt ? "std::int32_t" : "float") << ">(";
                        emitExpr(*e.args_[i]);
                        out_ << ")";
                    }
                    out_ << "}";
                    return isInt ? ExprType::IVEC3 : ExprType::VEC3;
                }
                const auto *schema = findComponent(e.componentName_);
                if (!schema) {
                    fail(file_, e.line_,
                         "component `" + e.componentName_ +
                             "` is not declared via `IRComponent.register(...)` "
                             "(CODEGEN constructors require codegen'd component types)");
                }
                if (e.args_.size() != schema->fields_.size()) {
                    fail(file_, e.line_,
                         "`" + e.componentName_ + ".new(...)` expects " +
                             std::to_string(schema->fields_.size()) + " argument(s), got " +
                             std::to_string(e.args_.size()));
                }
                out_ << "IRComponents::C_" << e.componentName_ << "(";
                for (size_t i = 0; i < e.args_.size(); ++i) {
                    if (i) out_ << ", ";
                    out_ << "static_cast<" << cppTypeForFieldType(schema->fields_[i].type_) << ">(";
                    emitExpr(*e.args_[i]);
                    out_ << ")";
                }
                out_ << ")";
                return ExprType::COMPONENT;
            }
        }
        return ExprType::UNKNOWN;
    }

    void emitStmt(const Stmt &s) {
        switch (s.kind_) {
            case StmtKind::LOCAL_DECL: {
                writeIndent();
                // Track type for the local before emitting.
                Symbol sym;
                ExprType t = ExprType::UNKNOWN;
                std::string componentName;
                // Peek: if RHS is COLUMN_AT or COMPONENT_NEW, we know the type.
                if (s.localInit_->kind_ == ExprKind::COLUMN_AT) {
                    t = ExprType::COMPONENT;
                    componentName = s.localInit_->componentName_;
                } else if (s.localInit_->kind_ == ExprKind::COMPONENT_NEW) {
                    if (s.localInit_->componentName_ == "vec3") {
                        t = ExprType::VEC3;
                    } else if (s.localInit_->componentName_ == "ivec3") {
                        t = ExprType::IVEC3;
                    } else {
                        t = ExprType::COMPONENT;
                        componentName = s.localInit_->componentName_;
                    }
                } else if (s.localInit_->kind_ == ExprKind::COLUMN_GET_FIELD) {
                    const auto *schema = findComponent(s.localInit_->componentName_);
                    if (schema) {
                        const auto *field = findField(*schema, s.localInit_->fieldNameLiteral_);
                        if (field) t = fieldTypeToExprType(field->type_);
                    }
                }
                // Emit. We use auto-deduction for component values (avoids namespace-spelling
                // headaches); for scalars, declare an explicit type so subsequent uses get
                // the right inference.
                if (t == ExprType::COMPONENT) {
                    // #1353: a read-only `local a = arch.C:at(i)` binding can
                    // alias the row (`const auto&`) instead of copying it.
                    // Only COLUMN_AT bindings are aliasable — a COMPONENT_NEW
                    // value is a temporary, so it stays by-value.
                    const bool aliasRow =
                        s.localInit_->kind_ == ExprKind::COLUMN_AT &&
                        refSafeColumnAtDecls_.count(&s) != 0;
                    out_ << (aliasRow ? "const auto& " : "auto ") << s.localName_ << " = ";
                    sym.type_ = ExprType::COMPONENT;
                    sym.componentName_ = componentName;
                    symbols_[s.localName_] = sym;
                    emitExpr(*s.localInit_);
                } else {
                    // Reserve the symbol entry pre-emission so recursive references
                    // (rare in DSL) still see the binding; finalize the type after.
                    symbols_[s.localName_] = Symbol{};
                    out_ << "auto " << s.localName_ << " = ";
                    ExprType rt = emitExpr(*s.localInit_);
                    Symbol &slot = symbols_[s.localName_];
                    slot.type_ = (t != ExprType::UNKNOWN) ? t : rt;
                }
                out_ << ";\n";
                return;
            }
            case StmtKind::ASSIGN: {
                writeIndent();
                switch (s.assignTarget_.kind_) {
                    case AssignTargetKind::NAME: {
                        auto it = symbols_.find(s.assignTarget_.name_);
                        if (it == symbols_.end()) {
                            fail(file_, s.line_,
                                 "assignment to unknown name `" + s.assignTarget_.name_ +
                                     "` (CODEGEN bodies must `local`-declare before assign)");
                        }
                        out_ << s.assignTarget_.name_ << " = ";
                        emitExpr(*s.assignRhs_);
                        out_ << ";\n";
                        return;
                    }
                    case AssignTargetKind::COLUMN_SET_AT: {
                        const auto *schema = findComponent(s.assignTarget_.componentName_);
                        if (!schema) {
                            fail(file_, s.line_,
                                 "component `" + s.assignTarget_.componentName_ +
                                     "` is not declared via `IRComponent.register(...)`");
                        }
                        if (!loopVarName_.empty()) {
                            if (!isLoopVarRef(*s.assignTarget_.indexExpr_)) {
                                fail(file_, s.line_,
                                     "`arch." + s.assignTarget_.componentName_ +
                                         ":setAt(...)` index must be the canonical loop "
                                         "variable `" + loopVarName_ + "`");
                            }
                            out_ << "_ir_row_" << s.assignTarget_.componentName_ << " = ";
                            emitExpr(*s.assignRhs_);
                            out_ << ";\n";
                            return;
                        }
                        out_ << "_ir_col_" << s.assignTarget_.componentName_ << "[";
                        emitExpr(*s.assignTarget_.indexExpr_);
                        out_ << "] = ";
                        emitExpr(*s.assignRhs_);
                        out_ << ";\n";
                        return;
                    }
                    case AssignTargetKind::COLUMN_SET_FIELD: {
                        const auto *schema = findComponent(s.assignTarget_.componentName_);
                        if (!schema) {
                            fail(file_, s.line_,
                                 "component `" + s.assignTarget_.componentName_ +
                                     "` is not declared via `IRComponent.register(...)`");
                        }
                        const auto *field = findField(*schema, s.assignTarget_.fieldNameLiteral_);
                        if (!field) {
                            fail(file_, s.line_,
                                 "component `" + schema->name_ + "` has no field `" +
                                     s.assignTarget_.fieldNameLiteral_ + "`");
                        }
                        if (!loopVarName_.empty()) {
                            if (!isLoopVarRef(*s.assignTarget_.indexExpr_)) {
                                fail(file_, s.line_,
                                     "`arch." + s.assignTarget_.componentName_ +
                                         ":setField(...)` index must be the canonical loop "
                                         "variable `" + loopVarName_ + "`");
                            }
                            out_ << "_ir_row_" << s.assignTarget_.componentName_ << "."
                                 << s.assignTarget_.fieldNameLiteral_ << "_ = static_cast<"
                                 << cppTypeForFieldType(field->type_) << ">(";
                            emitExpr(*s.assignRhs_);
                            out_ << ");\n";
                            return;
                        }
                        out_ << "_ir_col_" << s.assignTarget_.componentName_ << "[";
                        emitExpr(*s.assignTarget_.indexExpr_);
                        out_ << "]." << s.assignTarget_.fieldNameLiteral_ << "_ = static_cast<"
                             << cppTypeForFieldType(field->type_) << ">(";
                        emitExpr(*s.assignRhs_);
                        out_ << ");\n";
                        return;
                    }
                }
                return;
            }
            case StmtKind::IF: {
                for (size_t i = 0; i < s.ifBranches_.size(); ++i) {
                    writeIndent();
                    if (i == 0) out_ << "if ("; else out_ << "} else if (";
                    emitExpr(*s.ifBranches_[i].cond_);
                    out_ << ") {\n";
                    ++indent_;
                    auto savedSymbols = symbols_;
                    for (const auto &bs : s.ifBranches_[i].body_) emitStmt(*bs);
                    symbols_ = std::move(savedSymbols);
                    --indent_;
                }
                if (s.hasElse_) {
                    writeIndent();
                    out_ << "} else {\n";
                    ++indent_;
                    auto savedSymbols = symbols_;
                    for (const auto &bs : s.elseBody_) emitStmt(*bs);
                    symbols_ = std::move(savedSymbols);
                    --indent_;
                }
                writeIndent();
                out_ << "}\n";
                return;
            }
            case StmtKind::FOR_NUMERIC: {
                writeIndent();
                out_ << "for (std::int32_t " << s.forVar_ << " = ";
                emitExpr(*s.forLo_);
                out_ << "; " << s.forVar_ << " <= ";
                emitExpr(*s.forHi_);
                out_ << "; ++" << s.forVar_ << ") {\n";
                ++indent_;
                auto savedSymbols = symbols_;
                symbols_[s.forVar_] = Symbol{ExprType::INT32, ""};
                for (const auto &bs : s.forBody_) emitStmt(*bs);
                symbols_ = std::move(savedSymbols);
                --indent_;
                writeIndent();
                out_ << "}\n";
                return;
            }
            case StmtKind::EXPR_STMT: {
                // #1616: the parser only builds EXPR_STMT for a bare
                // INTRINSIC_CALL. Statement position is reserved for
                // whitelisted side-effecting (void) bindings — a bare
                // value-returning intrinsic is almost always a dropped
                // assignment, so reject it with a pointed message.
                const Expr &e = *s.exprStmt_;
                const Intrinsic &intr = resolveIntrinsic(e);
                if (!intr.isStatement_) {
                    fail(file_, e.line_,
                         "intrinsic `" + e.intrinsicNamespace_ + "." + e.intrinsicName_ +
                             "` returns a value; a bare call statement is only valid for a "
                             "whitelisted side-effecting binding (assign the result to a "
                             "`local` or a column field instead)");
                }
                if (intr.requiredInclude_) {
                    requiredIncludes_.insert(intr.requiredInclude_);
                }
                writeIndent();
                emitIntrinsicCall(e, intr);
                out_ << ";\n";
                return;
            }
            case StmtKind::RETURN: {
                writeIndent();
                out_ << "return";
                if (s.hasReturnExpr_) {
                    out_ << " ";
                    emitExpr(*s.returnExpr_);
                }
                out_ << ";\n";
                return;
            }
        }
    }

    std::string str() const { return out_.str(); }
};

} // namespace

const std::vector<Intrinsic> &intrinsicRegistry() { return kIntrinsicRegistry; }

const Intrinsic *findIntrinsic(const std::string &ns, const std::string &name) {
    for (const auto &i : kIntrinsicRegistry) {
        if (ns == i.luaNamespace_ && name == i.luaName_) return &i;
    }
    return nullptr;
}

ParsedBody parseSystemBody(
    const std::string &sourceFile,
    int bodyStartLine,
    const std::string &bodySource
) {
    auto tokens = tokenize(bodySource, sourceFile, bodyStartLine);
    Parser p(tokens, sourceFile);
    ParsedBody body;
    body.stmts_ = p.parseBlock(TokenKind::END_OF_INPUT);
    return body;
}

// T-347: identify the canonical per-row loop body. The shape:
//   for <loopVar> = 0, arch.length - 1 do <stmts> end
// at the top level of the tick body, as the ONLY top-level statement.
// Returns the for-statement pointer when matched, else nullptr. The
// pointer is borrowed from `body`; callers must not outlive it.
const Stmt *matchPerRowCanonicalLoop(const ParsedBody &body) {
    if (body.stmts_.size() != 1) return nullptr;
    const Stmt *s = body.stmts_[0].get();
    if (s->kind_ != StmtKind::FOR_NUMERIC) return nullptr;

    // Lo: integer literal 0.
    if (!s->forLo_ || s->forLo_->kind_ != ExprKind::NUMBER_LITERAL ||
        !s->forLo_->isInt_ || s->forLo_->intValue_ != 0) {
        return nullptr;
    }

    // Hi: `arch.length - 1` (BinOp SUB; lhs INDEX(arch, length); rhs int 1).
    const Expr *hi = s->forHi_.get();
    if (!hi || hi->kind_ != ExprKind::BINARY_OP || hi->binOp_ != BinOp::SUB) {
        return nullptr;
    }
    const Expr *hiLhs = hi->lhs_.get();
    const Expr *hiRhs = hi->rhs_.get();
    if (!hiLhs || hiLhs->kind_ != ExprKind::INDEX ||
        hiLhs->field_ != "length" ||
        !hiLhs->receiver_ || hiLhs->receiver_->kind_ != ExprKind::NAME_REF ||
        hiLhs->receiver_->name_ != "arch") {
        return nullptr;
    }
    if (!hiRhs || hiRhs->kind_ != ExprKind::NUMBER_LITERAL ||
        !hiRhs->isInt_ || hiRhs->intValue_ != 1) {
        return nullptr;
    }

    return s;
}

void emitSystem(
    std::string &out,
    const SystemRecord &record,
    const ParsedBody &body,
    const std::vector<ComponentSchema> &componentRegistry,
    std::set<std::string> &outRequiredIncludes
) {
    // Validate components.
    for (const auto &compName : record.components_) {
        bool found = false;
        for (const auto &c : componentRegistry) {
            if (c.name_ == compName) { found = true; break; }
        }
        if (!found) {
            ParseError err;
            err.message_ = "system `" + record.name_ +
                          "` references component `" + compName +
                          "` which is not declared via `IRComponent.register(...)` "
                          "(CODEGEN systems only support Lua-defined components in v1; "
                          "use `mode = \"eval\"` for systems that touch C++-bound types)";
            err.file_ = record.sourceFile_;
            err.line_ = record.linedefined_;
            throw err;
        }
    }
    for (const auto &excludeName : record.excludes_) {
        bool found = false;
        for (const auto &c : componentRegistry) {
            if (c.name_ == excludeName) { found = true; break; }
        }
        if (!found) {
            ParseError err;
            err.message_ = "system `" + record.name_ +
                          "` excludes component `" + excludeName +
                          "` which is not declared via `IRComponent.register(...)`";
            err.file_ = record.sourceFile_;
            err.line_ = record.linedefined_;
            throw err;
        }
    }

    // CODEGEN system bodies must follow the canonical per-row shape
    // so the emitted lambda is the per-component form
    // (`[](C_X& _ir_row_X, ...)`). Per-component is the engine's default
    // tick signature (engine/system/CLAUDE.md "Three valid TICK function
    // signatures") and the only one compatible with
    // `Concurrency::PARALLEL_FOR` — the batch form FATALs in
    // `detail::validateConcurrencyForAccess` (engine/system/include/irreden/
    // ir_system.hpp).
    const Stmt *perRowLoop = matchPerRowCanonicalLoop(body);
    if (!perRowLoop) {
        ParseError err;
        err.message_ = "system `" + record.name_ +
            "` body must consist of exactly one canonical per-row loop: "
            "`for i = 0, arch.length - 1 do ... end`. CODEGEN dispatches "
            "per-row so column ops outside the loop or computed loop bounds "
            "have no meaning. (Switch to `mode = \"eval\"` for bodies that "
            "need the batch form.)";
        err.file_ = record.sourceFile_;
        err.line_ = record.linedefined_;
        throw err;
    }

    Emitter emitter(record.sourceFile_, componentRegistry);

    std::ostringstream sig;
    sig << "inline IRSystem::SystemId createSystem_" << record.name_ << "() {\n";
    sig << "    return IRSystem::createSystem<\n";
    for (size_t i = 0; i < record.components_.size(); ++i) {
        sig << "        IRComponents::C_" << record.components_[i];
        const bool moreComponents = (i + 1 < record.components_.size());
        const bool hasExcludes = !record.excludes_.empty();
        if (moreComponents || hasExcludes) sig << ",";
        sig << "\n";
    }
    if (!record.excludes_.empty()) {
        sig << "        IRSystem::Exclude<\n";
        for (size_t i = 0; i < record.excludes_.size(); ++i) {
            sig << "            IRComponents::C_" << record.excludes_[i];
            if (i + 1 < record.excludes_.size()) sig << ",";
            sig << "\n";
        }
        sig << "        >\n";
    }
    sig << "    >(\n";
    sig << "        \"" << record.name_ << "\",\n";
    sig << "        [](";
    for (size_t i = 0; i < record.components_.size(); ++i) {
        if (i) sig << ",\n           ";
        sig << "IRComponents::C_" << record.components_[i] << "& _ir_row_"
            << record.components_[i];
    }
    sig << ") {\n";
    out += sig.str();

    emitter.loopVarName_ = perRowLoop->forVar_;
    emitter.indent_ = 3;
    // #1353: decide which `arch.C:at(i)` bindings can alias the row instead of
    // copying it, before emitting the body.
    collectRefSafeColumnAtDecls(perRowLoop->forBody_, emitter.refSafeColumnAtDecls_);
    for (const auto &s : perRowLoop->forBody_) emitter.emitStmt(*s);

    // #1616: surface the engine-binding headers this body's whitelisted
    // side-effecting calls need, so main.cpp can #include them.
    outRequiredIncludes.insert(emitter.requiredIncludes_.begin(),
                               emitter.requiredIncludes_.end());

    out += emitter.str();
    out += "        }\n";
    // T-223: thread the per-system concurrency value through to the
    // engine-side createSystem<>'s trailing Concurrency arg. The
    // trailing-argument shape requires the begin/end/relation tick + the
    // exclude-archetype slots to be filled first; pass the zero-value
    // sentinels (`nullptr` lambdas, default-constructed `RelationParams`,
    // and an empty `IREntity::Archetype`) so the named positional carries
    // the meaning. SERIAL is the no-op default and is emitted explicitly
    // for diffability — a reader sees the policy at a glance instead of
    // having to look up the function's default.
    const char *concCpp = "IRSystem::Concurrency::SERIAL";
    switch (record.concurrency_) {
        case Concurrency::SERIAL:       concCpp = "IRSystem::Concurrency::SERIAL"; break;
        case Concurrency::PARALLEL_FOR: concCpp = "IRSystem::Concurrency::PARALLEL_FOR"; break;
        case Concurrency::MAIN_THREAD:  concCpp = "IRSystem::Concurrency::MAIN_THREAD"; break;
    }
    out += "        ,\n";
    out += "        /* functionBeginTick */ nullptr,\n";
    out += "        /* functionEndTick */ nullptr,\n";
    out += "        /* extraParams */ {},\n";
    out += "        /* functionRelationTick */ nullptr,\n";
    out += "        /* concurrency */ ";
    out += concCpp;
    out += "\n";
    out += "    );\n";
    out += "}\n\n";
}

std::string sliceFunctionBody(
    const std::string &sourceFile,
    int linedefined,
    int lastlinedefined,
    int &outBodyStartLine
) {
    std::ifstream in(sourceFile);
    if (!in) {
        ParseError err;
        err.message_ = "lua_codegen: cannot reopen source file for body extraction";
        err.file_ = sourceFile;
        err.line_ = linedefined;
        throw err;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);

    if (linedefined < 1 || lastlinedefined > static_cast<int>(lines.size()) ||
        lastlinedefined < linedefined) {
        ParseError err;
        err.message_ = "lua_codegen: function source location out of range "
                       "(linedefined=" + std::to_string(linedefined) +
                       " lastlinedefined=" + std::to_string(lastlinedefined) +
                       " file lines=" + std::to_string(lines.size()) + ")";
        err.file_ = sourceFile;
        err.line_ = linedefined;
        throw err;
    }

    // Concatenate the function span and find the parameter-list closing `)` after
    // the first `function` keyword. Our DSL has only `function(arch)` so the search
    // is robust.
    std::string concat;
    for (int i = linedefined - 1; i < lastlinedefined; ++i) {
        concat += lines[i];
        concat.push_back('\n');
    }

    // Find `function`, then the matching `)`.
    const auto fnPos = concat.find("function");
    if (fnPos == std::string::npos) {
        ParseError err;
        err.message_ = "lua_codegen: could not locate `function` keyword in body span";
        err.file_ = sourceFile;
        err.line_ = linedefined;
        throw err;
    }
    auto openParen = concat.find('(', fnPos);
    if (openParen == std::string::npos) {
        ParseError err;
        err.message_ = "lua_codegen: missing `(` after `function`";
        err.file_ = sourceFile;
        err.line_ = linedefined;
        throw err;
    }
    int depth = 1;
    auto closeParen = openParen + 1;
    while (closeParen < concat.size() && depth > 0) {
        if (concat[closeParen] == '(') ++depth;
        else if (concat[closeParen] == ')') --depth;
        if (depth == 0) break;
        ++closeParen;
    }
    if (depth != 0) {
        ParseError err;
        err.message_ = "lua_codegen: unbalanced `(` in function parameter list";
        err.file_ = sourceFile;
        err.line_ = linedefined;
        throw err;
    }

    // Body starts after the `)` and continues up to (but not including) the closing
    // `end`. We don't try to parse-balance Lua here — we trust Lua already accepted
    // the function literal, so the last `end` token within the span is the one that
    // closes our function. To find it robustly we count `end`-introducing tokens.
    //
    // Simpler v1 strategy: take everything after the `)` up through the LAST occurrence
    // of `end` on `lastlinedefined` (which is what Lua's debug info reports). The
    // tokenizer will catch any malformed body downstream.
    const size_t bodyStart = closeParen + 1;

    // Compute the offset (within `concat`) of the start of the last line.
    size_t lastLineOffset = 0;
    int currentLine = linedefined;
    for (size_t i = 0; i < concat.size(); ++i) {
        if (currentLine == lastlinedefined) {
            lastLineOffset = i;
            break;
        }
        if (concat[i] == '\n') ++currentLine;
    }
    // Find the LAST `end` (as a whole word) on the last line.
    // v1 limitation: scans rightmost to leftmost, so a line like
    //   `end -- end of tick`
    // matches the comment's `end` first and produces a malformed slice that
    // fails with a parse error downstream. Workaround: the closing `end` of
    // the tick function must not appear in an inline comment on that same line.
    const std::string lastLine = concat.substr(lastLineOffset);
    auto isWordBoundary = [](char c) { return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_'); };
    size_t bodyEnd = std::string::npos;
    for (size_t i = lastLine.size(); i >= 3; --i) {
        if (lastLine[i - 3] == 'e' && lastLine[i - 2] == 'n' && lastLine[i - 1] == 'd') {
            const bool leftOk = (i - 3 == 0) || isWordBoundary(lastLine[i - 4]);
            const bool rightOk = (i == lastLine.size()) || isWordBoundary(lastLine[i]);
            if (leftOk && rightOk) {
                bodyEnd = lastLineOffset + (i - 3);
                break;
            }
        }
    }
    if (bodyEnd == std::string::npos || bodyEnd <= bodyStart) {
        ParseError err;
        err.message_ = "lua_codegen: could not locate closing `end` for function body";
        err.file_ = sourceFile;
        err.line_ = lastlinedefined;
        throw err;
    }

    // Compute the line in the source file of the body's first character.
    int bodyFirstLine = linedefined;
    for (size_t i = 0; i < bodyStart; ++i) if (concat[i] == '\n') ++bodyFirstLine;
    outBodyStartLine = bodyFirstLine;

    return concat.substr(bodyStart, bodyEnd - bodyStart);
}

} // namespace IRLuaCodegen
