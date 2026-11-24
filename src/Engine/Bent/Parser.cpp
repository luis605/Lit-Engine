#include <limits>
#include <set>
#include <string>
#include <vector>

#include "Engine/Bent/Ast.hpp"

namespace bent {

namespace {

const std::set<std::string>& keywords() {
    static const std::set<std::string> words = {"let", "in", "if", "then", "else", "fn", "forall", "exists", "after", "always", "eventually", "within",
                                                "and", "or", "not", "true", "false", "nan", "inf", "unit", "law", "include", "example", "known", "where"};
    return words;
}

class Parser {
  public:
    explicit Parser(const std::vector<Token>& tokens) : m_tokens(tokens) {}

    std::vector<Decl> program() {
        std::vector<Decl> decls;
        while (!atEnd()) decls.push_back(decl());
        return decls;
    }

    ExprPtr single() {
        ExprPtr e = expr();
        if (!atEnd()) fail("unexpected '" + cur().text + "' after expression");
        return e;
    }

  private:
    const std::vector<Token>& m_tokens;
    size_t m_pos = 0;

    const Token& cur() const { return m_tokens[m_pos]; }
    const Token& ahead(size_t offset) const { return m_tokens[std::min(m_pos + offset, m_tokens.size() - 1)]; }
    bool atEnd() const { return cur().kind == TokKind::End; }
    [[noreturn]] void fail(const std::string& message) const { throw BentError(cur().loc, message); }
    bool isOp(const char* op) const { return cur().kind == TokKind::Op && cur().text == op; }
    bool isWord(const char* word) const { return cur().kind == TokKind::Ident && cur().text == word; }
    bool acceptOp(const char* op) {
        if (!isOp(op)) return false;
        ++m_pos;
        return true;
    }
    bool acceptWord(const char* word) {
        if (!isWord(word)) return false;
        ++m_pos;
        return true;
    }
    void expectOp(const char* op) {
        if (!acceptOp(op)) fail(std::string("expected '") + op + "', found " + describe(cur()));
    }
    void expectWord(const char* word) {
        if (!acceptWord(word)) fail(std::string("expected '") + word + "', found " + describe(cur()));
    }
    static std::string describe(const Token& token) {
        switch (token.kind) {
        case TokKind::End: return "end of input";
        case TokKind::String: return "string " + quoteString(token.text);
        default: return "'" + token.text + "'";
        }
    }
    std::string ident(const char* what) {
        if (cur().kind != TokKind::Ident || keywords().contains(cur().text)) fail(std::string("expected ") + what + ", found " + describe(cur()));
        return m_tokens[m_pos++].text;
    }

    static std::shared_ptr<Expr> node(ExprKind kind, const SrcLoc& loc) {
        auto e = std::make_shared<Expr>();
        e->kind = kind;
        e->loc = loc;
        return e;
    }

    std::vector<std::string> paramList() {
        std::vector<std::string> params;
        expectOp("(");
        if (!acceptOp(")")) {
            do {
                params.push_back(ident("parameter name"));
            } while (acceptOp(","));
            expectOp(")");
        }
        return params;
    }

    Decl decl() {
        Decl d;
        d.loc = cur().loc;
        if (acceptWord("include")) {
            if (cur().kind != TokKind::String) fail("expected a file name string after include");
            d.kind = DeclKind::Include;
            d.name = m_tokens[m_pos++].text;
            return d;
        }
        if (acceptWord("let") || acceptWord("gen")) {
            d.kind = DeclKind::Let;
            d.name = ident("name");
            expectOp("=");
            d.value = expr();
            return d;
        }
        if (acceptWord("fn")) {
            d.kind = DeclKind::Fn;
            d.name = ident("function name");
            d.params = paramList();
            expectOp("=");
            d.value = expr();
            return d;
        }
        if (acceptWord("law")) {
            d.kind = DeclKind::Law;
            auto law = std::make_shared<LawDecl>();
            law->loc = d.loc;
            law->file = d.loc.file;
            law->name = ident("law name");
            d.name = law->name;
            expectOp("{");
            bool header = false;
            if (isWord("forall") && ahead(1).kind == TokKind::Op && ahead(1).text == "(") {
                header = true;
                ++m_pos;
                expectOp("(");
                if (!acceptOp(")")) {
                    do {
                        const std::string name = ident("variable name");
                        expectOp(":");
                        law->vars.emplace_back(name, expr());
                    } while (acceptOp(","));
                    expectOp(")");
                }
            }
            if (acceptWord("where")) {
                header = true;
                law->where = expr();
            }
            if (header) expectOp("=>");
            else acceptOp("=>");
            law->property = expr();
            expectOp("}");
            d.law = law;
            return d;
        }
        if (acceptWord("example")) {
            d.kind = DeclKind::Example;
            auto ex = std::make_shared<ExampleDecl>();
            ex->loc = d.loc;
            ex->file = d.loc.file;
            ex->law = ident("law name");
            if (acceptWord("known")) {
                ex->known = true;
                if (cur().kind == TokKind::String) ex->reason = m_tokens[m_pos++].text;
            }
            expectOp("{");
            while (!acceptOp("}")) {
                const std::string name = ident("variable name");
                expectOp("=");
                ex->bindings.emplace_back(name, expr());
                acceptOp(",");
            }
            d.example = ex;
            return d;
        }
        fail("expected a declaration (include, let, gen, fn, law, example), found " + describe(cur()));
    }

    ExprPtr expr() { return implies(); }

    ExprPtr implies() {
        ExprPtr left = orExpr();
        if (isOp("==>")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Binary, loc);
            e->name = "==>";
            e->args = {left, implies()};
            return e;
        }
        return left;
    }

    ExprPtr orExpr() {
        ExprPtr left = andExpr();
        while (isWord("or")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Binary, loc);
            e->name = "or";
            e->args = {left, andExpr()};
            left = e;
        }
        return left;
    }

    ExprPtr andExpr() {
        ExprPtr left = notExpr();
        while (isWord("and")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Binary, loc);
            e->name = "and";
            e->args = {left, notExpr()};
            left = e;
        }
        return left;
    }

    ExprPtr notExpr() {
        if (isWord("not")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Unary, loc);
            e->name = "not";
            e->args = {notExpr()};
            return e;
        }
        return comparison();
    }

    ExprPtr comparison() {
        ExprPtr left = concat();
        static const char* const ops[] = {"==", "!=", "<=", ">=", "<", ">"};
        for (const char* op : ops) {
            if (isOp(op)) {
                const SrcLoc loc = cur().loc;
                ++m_pos;
                auto e = node(ExprKind::Binary, loc);
                e->name = op;
                e->args = {left, concat()};
                return e;
            }
        }
        if (isOp("~=")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Approx, loc);
            e->args = {left, concat()};
            if (acceptWord("within")) e->args.push_back(concat());
            return e;
        }
        return left;
    }

    ExprPtr concat() {
        ExprPtr left = additive();
        while (isOp("++")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Binary, loc);
            e->name = "++";
            e->args = {left, additive()};
            left = e;
        }
        return left;
    }

    ExprPtr additive() {
        ExprPtr left = multiplicative();
        while (isOp("+") || isOp("-")) {
            const SrcLoc loc = cur().loc;
            const std::string op = m_tokens[m_pos++].text;
            auto e = node(ExprKind::Binary, loc);
            e->name = op;
            e->args = {left, multiplicative()};
            left = e;
        }
        return left;
    }

    ExprPtr multiplicative() {
        ExprPtr left = unary();
        while (isOp("*") || isOp("/") || isOp("%")) {
            const SrcLoc loc = cur().loc;
            const std::string op = m_tokens[m_pos++].text;
            auto e = node(ExprKind::Binary, loc);
            e->name = op;
            e->args = {left, unary()};
            left = e;
        }
        return left;
    }

    ExprPtr unary() {
        if (isOp("-")) {
            const SrcLoc loc = cur().loc;
            ++m_pos;
            auto e = node(ExprKind::Unary, loc);
            e->name = "-";
            e->args = {unary()};
            return e;
        }
        return postfix();
    }

    void callArgs(Expr& call) {
        expectOp("(");
        if (acceptOp(")")) return;
        do {
            if (cur().kind == TokKind::Ident && !keywords().contains(cur().text) && ahead(1).kind == TokKind::Op && ahead(1).text == ":") {
                const std::string name = m_tokens[m_pos].text;
                m_pos += 2;
                for (const auto& existing : call.named) {
                    if (existing.first == name) fail("duplicate argument '" + name + "'");
                }
                call.named.emplace_back(name, expr());
            } else {
                if (!call.named.empty()) fail("positional argument after named arguments");
                call.args.push_back(expr());
            }
        } while (acceptOp(","));
        expectOp(")");
    }

    ExprPtr postfix() {
        ExprPtr e = primary();
        while (true) {
            if (isOp("(")) {
                auto call = node(ExprKind::Call, cur().loc);
                call->args.push_back(e);
                callArgs(*call);
                e = call;
            } else if (isOp(".")) {
                const SrcLoc loc = cur().loc;
                ++m_pos;
                auto field = node(ExprKind::Field, loc);
                if (cur().kind != TokKind::Ident) fail("expected a field name after '.'");
                field->name = m_tokens[m_pos++].text;
                field->args = {e};
                e = field;
            } else if (isOp("[")) {
                const SrcLoc loc = cur().loc;
                ++m_pos;
                auto index = node(ExprKind::Index, loc);
                index->args = {e, expr()};
                expectOp("]");
                e = index;
            } else {
                return e;
            }
        }
    }

    ExprPtr temporal(TemporalKind kind, const SrcLoc& loc, ExprPtr ticks) {
        expectWord("after");
        auto e = node(ExprKind::Temporal, loc);
        e->temporal = kind;
        expectOp("(");
        ExprPtr program = expr();
        expectOp(")");
        expectOp("{");
        ExprPtr body = expr();
        expectOp("}");
        e->args = {program, body};
        if (ticks) e->args.push_back(ticks);
        return e;
    }

    ExprPtr primary() {
        const Token& token = cur();
        const SrcLoc loc = token.loc;
        switch (token.kind) {
        case TokKind::Int: {
            ++m_pos;
            auto e = node(ExprKind::Lit, loc);
            e->literal = Value::integer(token.ival);
            return e;
        }
        case TokKind::Float: {
            ++m_pos;
            auto e = node(ExprKind::Lit, loc);
            e->literal = Value::number(token.fval);
            return e;
        }
        case TokKind::String: {
            ++m_pos;
            auto e = node(ExprKind::Lit, loc);
            e->literal = Value::string(token.text);
            return e;
        }
        case TokKind::End: fail("unexpected end of input, expected an expression");
        case TokKind::Op: break;
        case TokKind::Ident: {
            const std::string& word = token.text;
            if (word == "true" || word == "false") {
                ++m_pos;
                auto e = node(ExprKind::Lit, loc);
                e->literal = Value::boolean(word == "true");
                return e;
            }
            if (word == "nan" || word == "inf") {
                ++m_pos;
                auto e = node(ExprKind::Lit, loc);
                e->literal = Value::number(word == "nan" ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity());
                return e;
            }
            if (word == "unit") {
                ++m_pos;
                return node(ExprKind::Lit, loc);
            }
            if (word == "let") {
                ++m_pos;
                auto e = node(ExprKind::Let, loc);
                e->name = ident("variable name");
                expectOp("=");
                ExprPtr value = expr();
                expectWord("in");
                e->args = {value, expr()};
                return e;
            }
            if (word == "if") {
                ++m_pos;
                auto e = node(ExprKind::If, loc);
                ExprPtr cond = expr();
                expectWord("then");
                ExprPtr yes = expr();
                expectWord("else");
                e->args = {cond, yes, expr()};
                return e;
            }
            if (word == "fn") {
                ++m_pos;
                auto e = node(ExprKind::Lambda, loc);
                e->params = paramList();
                expectOp("=>");
                e->args = {expr()};
                return e;
            }
            if (word == "forall" || word == "exists") {
                ++m_pos;
                auto e = node(ExprKind::Quant, loc);
                e->flag = word == "forall";
                e->name = ident("variable name");
                expectWord("in");
                ExprPtr domain = expr();
                expectOp(":");
                e->args = {domain, expr()};
                return e;
            }
            if (word == "after") return temporal(TemporalKind::After, loc, nullptr);
            if (word == "always") {
                ++m_pos;
                ExprPtr ticks;
                if (acceptWord("within")) ticks = additive();
                return temporal(TemporalKind::Always, loc, ticks);
            }
            if (word == "eventually") {
                ++m_pos;
                expectWord("within");
                ExprPtr ticks = additive();
                return temporal(TemporalKind::Eventually, loc, ticks);
            }
            if (keywords().contains(word)) fail("unexpected keyword '" + word + "'");
            ++m_pos;
            auto e = node(ExprKind::Var, loc);
            e->name = word;
            return e;
        }
        }
        if (acceptOp("(")) {
            ExprPtr inner = expr();
            expectOp(")");
            return inner;
        }
        if (acceptOp("[")) {
            auto e = node(ExprKind::List, loc);
            if (!acceptOp("]")) {
                do {
                    if (isOp("]")) break;
                    e->args.push_back(expr());
                } while (acceptOp(","));
                expectOp("]");
            }
            return e;
        }
        if (acceptOp("{")) {
            auto e = node(ExprKind::Record, loc);
            if (!acceptOp("}")) {
                do {
                    if (isOp("}")) break;
                    std::string key;
                    if (cur().kind == TokKind::String || cur().kind == TokKind::Ident) key = m_tokens[m_pos++].text;
                    else fail("expected a field name in record literal, found " + describe(cur()));
                    expectOp(":");
                    for (const auto& existing : e->named) {
                        if (existing.first == key) fail("duplicate field '" + key + "'");
                    }
                    e->named.emplace_back(key, expr());
                } while (acceptOp(","));
                expectOp("}");
            }
            return e;
        }
        fail("expected an expression, found " + describe(token));
    }
};

}

std::vector<Decl> parse(const std::vector<Token>& tokens) { return Parser(tokens).program(); }

ExprPtr parseExpression(const std::vector<Token>& tokens) { return Parser(tokens).single(); }

}
