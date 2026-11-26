#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Bent/Value.hpp"

namespace bent {

enum class TokKind : uint8_t { Ident, Int, Float, String, Op, End };

struct Token {
    TokKind kind = TokKind::End;
    std::string text;
    int64_t ival = 0;
    double fval = 0.0;
    SrcLoc loc;
};

[[nodiscard]] std::vector<Token> lex(const std::string& source, const std::string& file);

enum class ExprKind : uint8_t { Lit, Var, Call, List, Record, Field, Index, Unary, Binary, If, Let, Lambda, Quant, Temporal, Approx };

enum class TemporalKind : uint8_t { After, Always, Eventually };

struct Expr {
    ExprKind kind = ExprKind::Lit;
    SrcLoc loc;
    Value literal;
    std::string name;
    std::vector<std::shared_ptr<const Expr>> args;
    std::vector<std::pair<std::string, std::shared_ptr<const Expr>>> named;
    std::vector<std::string> params;
    bool flag = false;
    TemporalKind temporal = TemporalKind::After;
};

using ExprPtr = std::shared_ptr<const Expr>;

struct LawDecl {
    std::string name;
    SrcLoc loc;
    std::vector<std::pair<std::string, ExprPtr>> vars;
    ExprPtr where;
    ExprPtr property;
    std::string file;
};

struct ExampleDecl {
    std::string law;
    SrcLoc loc;
    bool known = false;
    std::string reason;
    std::vector<std::pair<std::string, ExprPtr>> bindings;
    std::string file;
};

enum class DeclKind : uint8_t { Include, Let, Fn, Law, Example };

struct Decl {
    DeclKind kind = DeclKind::Let;
    SrcLoc loc;
    std::string name;
    std::vector<std::string> params;
    ExprPtr value;
    std::shared_ptr<LawDecl> law;
    std::shared_ptr<ExampleDecl> example;
};

[[nodiscard]] std::vector<Decl> parse(const std::vector<Token>& tokens);
[[nodiscard]] ExprPtr parseExpression(const std::vector<Token>& tokens);

}
