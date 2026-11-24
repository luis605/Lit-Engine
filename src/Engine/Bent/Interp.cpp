#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>

#include "Engine/Bent/Interp.hpp"

namespace bent {

EnvPtr bind(EnvPtr parent, std::string name, Value value) {
    auto env = std::make_shared<Env>();
    env->name = std::move(name);
    env->value = std::move(value);
    env->parent = std::move(parent);
    return env;
}

std::string truncate(const std::string& text, size_t limit) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit) + "...";
}

double argNumber(const Value& value, const SrcLoc& loc, const char* what) {
    if (!value.isNumber()) throw BentError(loc, std::string(what) + ": expected a number, got " + kindName(value.kind) + " " + truncate(print(value), 80));
    return value.num();
}

int64_t argInt(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Int) return value.i;
    if (value.kind == Kind::Float && std::isfinite(value.f) && std::floor(value.f) == value.f && std::fabs(value.f) < 9.0e18) return static_cast<int64_t>(value.f);
    throw BentError(loc, std::string(what) + ": expected an int, got " + kindName(value.kind) + " " + truncate(print(value), 80));
}

const std::string& argString(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind != Kind::Str) throw BentError(loc, std::string(what) + ": expected a string, got " + kindName(value.kind));
    return value.text();
}

const ValueList& argList(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind != Kind::List) throw BentError(loc, std::string(what) + ": expected a list, got " + kindName(value.kind) + " " + truncate(print(value), 80));
    return value.items();
}

const LawDecl* Program::law(const std::string& name) const {
    for (const auto& law : laws) {
        if (law->name == name) return law.get();
    }
    return nullptr;
}

void Program::define(const std::string& name, Value value, const SrcLoc& loc) {
    if (globals.contains(name)) throw BentError(loc, "redefinition of '" + name + "'");
    globals.emplace(name, std::move(value));
}

void Program::native(const std::string& name, NativeFn fn, bool liftable) {
    auto f = std::make_shared<FuncV>();
    f->name = name;
    f->native = std::move(fn);
    f->liftable = liftable;
    Value value;
    value.kind = Kind::Func;
    value.fn = std::move(f);
    globals.insert_or_assign(name, std::move(value));
}

// ---------------------------------------------------------------- loading

namespace {

struct Loader {
    Program& program;
    std::set<std::string> loaded;
    std::vector<std::string> stack;

    void loadFile(const std::string& path, const SrcLoc& from) {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        const std::string key = ec ? path : canonical.string();
        if (std::find(stack.begin(), stack.end(), key) != stack.end()) throw BentError(from, "include cycle through '" + path + "'");
        if (loaded.contains(key)) return;
        std::ifstream in(path, std::ios::binary);
        if (!in) throw BentError(from, "cannot open '" + path + "'");
        std::stringstream buffer;
        buffer << in.rdbuf();
        loaded.insert(key);
        stack.push_back(key);
        loadSource(buffer.str(), path);
        stack.pop_back();
    }

    void loadSource(const std::string& source, const std::string& name) {
        program.files.push_back(name);
        const std::vector<Decl> decls = parse(lex(source, name));
        const std::filesystem::path dir = std::filesystem::path(name).parent_path();
        // Functions are hoisted so lets and other functions can refer to them in any order within a file.
        for (const Decl& decl : decls) {
            if (decl.kind != DeclKind::Fn) continue;
            auto f = std::make_shared<FuncV>();
            f->name = decl.name;
            f->params = decl.params;
            f->body = decl.value;
            Value value;
            value.kind = Kind::Func;
            value.fn = std::move(f);
            program.define(decl.name, std::move(value), decl.loc);
        }
        for (const Decl& decl : decls) {
            switch (decl.kind) {
            case DeclKind::Include: loadFile((dir / decl.name).string(), decl.loc); break;
            case DeclKind::Let: {
                Interp interp(program);
                Value value = interp.eval(decl.value, nullptr);
                program.define(decl.name, std::move(value), decl.loc);
                break;
            }
            case DeclKind::Law:
                if (program.law(decl.law->name)) throw BentError(decl.loc, "duplicate law '" + decl.law->name + "'");
                program.laws.push_back(decl.law);
                break;
            case DeclKind::Example: program.examples.push_back(decl.example); break;
            case DeclKind::Fn: break;
            }
        }
    }
};

}

std::unique_ptr<Program> loadProgram(const std::string& path, Host* host) {
    auto program = std::make_unique<Program>();
    program->host = host;
    installBuiltins(*program);
    if (host) host->addBuiltins(*program);
    Loader loader{*program, {}, {}};
    loader.loadFile(path, SrcLoc{path, 0, 0});
    return program;
}

std::unique_ptr<Program> loadProgramSource(const std::string& source, const std::string& name, Host* host) {
    auto program = std::make_unique<Program>();
    program->host = host;
    installBuiltins(*program);
    if (host) host->addBuiltins(*program);
    Loader loader{*program, {}, {}};
    loader.loadSource(source, name);
    return program;
}

// ---------------------------------------------------------------- evaluation

namespace {

struct Quiet {
    int& q;
    explicit Quiet(int& counter) : q(counter) { ++q; }
    ~Quiet() { --q; }
};

const Value* lookup(const EnvPtr& env, const std::string& name) {
    for (const Env* scope = env.get(); scope; scope = scope->parent.get()) {
        if (scope->name == name) return &scope->value;
    }
    return nullptr;
}

std::string brief(const Value& value) { return truncate(print(value), 200); }

[[noreturn]] void typeError(const SrcLoc& loc, const std::string& op, const Value& a, const Value& b) {
    throw BentError(loc, "cannot apply '" + op + "' to " + kindName(a.kind) + " and " + kindName(b.kind));
}

Value arithmetic(const std::string& op, const Value& a, const Value& b, const SrcLoc& loc) {
    if (a.kind == Kind::Int && b.kind == Kind::Int) {
        int64_t out = 0;
        if (op == "+") {
            if (__builtin_add_overflow(a.i, b.i, &out)) throw BentError(loc, "integer overflow in +");
        } else if (op == "-") {
            if (__builtin_sub_overflow(a.i, b.i, &out)) throw BentError(loc, "integer overflow in -");
        } else if (op == "*") {
            if (__builtin_mul_overflow(a.i, b.i, &out)) throw BentError(loc, "integer overflow in *");
        } else if (op == "/" || op == "%") {
            if (b.i == 0) throw BentError(loc, "integer division by zero");
            if (a.i == std::numeric_limits<int64_t>::min() && b.i == -1) throw BentError(loc, "integer overflow in " + op);
            out = op == "/" ? a.i / b.i : a.i % b.i;
        }
        return Value::integer(out);
    }
    if (a.isNumber() && b.isNumber()) {
        const double x = a.num();
        const double y = b.num();
        if (op == "+") return Value::number(x + y);
        if (op == "-") return Value::number(x - y);
        if (op == "*") return Value::number(x * y);
        if (op == "/") return Value::number(x / y);
        return Value::number(std::fmod(x, y));
    }
    if (a.kind == Kind::Vec3 && b.kind == Kind::Vec3 && (op == "+" || op == "-")) {
        const double s = op == "+" ? 1.0 : -1.0;
        return Value::vec3(a.v[0] + s * b.v[0], a.v[1] + s * b.v[1], a.v[2] + s * b.v[2]);
    }
    if (a.kind == Kind::Vec3 && b.isNumber() && (op == "*" || op == "/")) {
        const double s = op == "*" ? b.num() : 1.0 / b.num();
        return Value::vec3(a.v[0] * s, a.v[1] * s, a.v[2] * s);
    }
    if (a.isNumber() && b.kind == Kind::Vec3 && op == "*") return Value::vec3(b.v[0] * a.num(), b.v[1] * a.num(), b.v[2] * a.num());
    if (a.kind == Kind::Quat && b.kind == Kind::Quat && op == "*") {
        const double aw = a.v[0], ax = a.v[1], ay = a.v[2], az = a.v[3];
        const double bw = b.v[0], bx = b.v[1], by = b.v[2], bz = b.v[3];
        return Value::quat(aw * bw - ax * bx - ay * by - az * bz, aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw);
    }
    if (a.kind == Kind::Quat && b.kind == Kind::Vec3 && op == "*") {
        const double w = a.v[0], qx = a.v[1], qy = a.v[2], qz = a.v[3];
        const double vx = b.v[0], vy = b.v[1], vz = b.v[2];
        const double tx = 2.0 * (qy * vz - qz * vy), ty = 2.0 * (qz * vx - qx * vz), tz = 2.0 * (qx * vy - qy * vx);
        return Value::vec3(vx + w * tx + (qy * tz - qz * ty), vy + w * ty + (qz * tx - qx * tz), vz + w * tz + (qx * ty - qy * tx));
    }
    if (a.kind == Kind::Str && b.kind == Kind::Str && op == "+") return Value::string(a.text() + b.text());
    typeError(loc, op, a, b);
}

bool ordered(const std::string& op, const Value& a, const Value& b, const SrcLoc& loc) {
    int c = 0;
    if (a.isNumber() && b.isNumber()) {
        const double x = a.num();
        const double y = b.num();
        if (op == "<") return x < y;
        if (op == "<=") return x <= y;
        if (op == ">") return x > y;
        return x >= y;
    }
    if (a.kind == Kind::Str && b.kind == Kind::Str) c = a.text().compare(b.text());
    else typeError(loc, op, a, b);
    if (op == "<") return c < 0;
    if (op == "<=") return c <= 0;
    if (op == ">") return c > 0;
    return c >= 0;
}

}

void Interp::note(std::string text) {
    if (!explain || quiet > 0 || notes.size() >= 40) return;
    notes.push_back(std::move(text));
}

Sim& Interp::currentSim(const SrcLoc& loc, const std::string& what) {
    if (sims.empty()) throw BentError(loc, "observation '" + what + "' needs a world: use it inside after(program) { ... }, always or eventually");
    return *sims.back();
}

Value Interp::call(const Value& fnValue, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
    if (fnValue.kind != Kind::Func) throw BentError(loc, std::string("cannot call a ") + kindName(fnValue.kind) + " (" + brief(fnValue) + ")");
    const FuncV& fn = *fnValue.fn;
    if (++depth > 600) {
        depth = 0;
        throw BentError(loc, "recursion too deep (more than 600 nested calls) in '" + fn.name + "'");
    }
    struct DepthGuard {
        int& d;
        ~DepthGuard() {
            if (d > 0) --d;
        }
    } guard{depth};
    if (fn.native) {
        if (fn.liftable) {
            bool lifted = false;
            for (const Value& arg : args) lifted = lifted || containsGen(arg);
            for (const auto& arg : named) lifted = lifted || containsGen(arg.second);
            if (lifted) {
                auto gen = std::make_shared<GenV>();
                gen->kind = GenKind::Lift;
                gen->values = args;
                gen->named = named;
                gen->lift = fn.native;
                gen->label = fn.name;
                gen->loc = loc;
                return makeGen(std::move(gen));
            }
        }
        return fn.native(*this, args, named, loc);
    }
    EnvPtr env = fn.closure;
    std::vector<bool> bound(fn.params.size(), false);
    if (args.size() > fn.params.size()) throw BentError(loc, "'" + fn.name + "' takes " + std::to_string(fn.params.size()) + " arguments, got " + std::to_string(args.size()));
    for (size_t index = 0; index < args.size(); ++index) {
        env = bind(env, fn.params[index], args[index]);
        bound[index] = true;
    }
    for (const auto& [key, value] : named) {
        const auto it = std::find(fn.params.begin(), fn.params.end(), key);
        if (it == fn.params.end()) throw BentError(loc, "'" + fn.name + "' has no parameter '" + key + "'");
        const size_t index = static_cast<size_t>(it - fn.params.begin());
        if (bound[index]) throw BentError(loc, "parameter '" + key + "' given twice");
        bound[index] = true;
        env = bind(env, key, value);
    }
    for (size_t index = 0; index < bound.size(); ++index) {
        if (!bound[index]) throw BentError(loc, "missing argument '" + fn.params[index] + "' in call to '" + fn.name + "'");
    }
    return eval(fn.body, env);
}

std::vector<const ActionV*> programActions(const Value& program, const SrcLoc& loc) {
    std::vector<const ActionV*> actions;
    if (program.kind == Kind::Action) {
        actions.push_back(program.act.get());
        return actions;
    }
    if (program.kind == Kind::Gen) throw BentError(loc, "a scene program must be a list of actions, got a generator (bind it with forall (p: ...) first)");
    if (program.kind != Kind::List) throw BentError(loc, std::string("a scene program must be a list of actions, got ") + kindName(program.kind));
    std::vector<const Value*> stack;
    for (auto it = program.items().rbegin(); it != program.items().rend(); ++it) stack.push_back(&*it);
    while (!stack.empty()) {
        const Value* item = stack.back();
        stack.pop_back();
        if (item->kind == Kind::Action) {
            actions.push_back(item->act.get());
        } else if (item->kind == Kind::List) {  // nested programs are flattened
            for (auto it = item->items().rbegin(); it != item->items().rend(); ++it) stack.push_back(&*it);
        } else {
            throw BentError(loc, std::string("scene program element is a ") + kindName(item->kind) + ", not an action: " + brief(*item));
        }
    }
    return actions;
}

Value Interp::runTemporal(TemporalKind kind, const Value& program, int64_t extraTicks, const std::function<Value()>& body, const SrcLoc& loc) {
    if (!prog.host) throw BentError(loc, "scene programs need a host (run through bent check)");
    const std::vector<const ActionV*> actions = programActions(program, loc);
    std::unique_ptr<Sim> sim = prog.host->createSim();
    sims.push_back(sim.get());
    struct Pop {
        std::vector<Sim*>& s;
        ~Pop() { s.pop_back(); }
    } pop{sims};
    const auto check = [&]() -> bool {
        const Value result = body();
        return truthy(result, loc);
    };
    if (kind == TemporalKind::After) {
        for (const ActionV* action : actions) sim->apply(*action, loc, [] { return true; });
        return body();
    }
    if (kind == TemporalKind::Always) {
        bool ok = true;
        size_t actionIndex = 0;
        const auto onTick = [&]() {
            if (!ok) return false;
            if (!check()) {
                ok = false;
                note(loc.str() + ": always failed at tick " + std::to_string(sim->ticks()) + " during action #" + std::to_string(actionIndex) + " " + truncate(print(Value::action(actions[actionIndex]->name, actions[actionIndex]->args)), 160));
            }
            return ok;
        };
        if (!check()) {
            note(loc.str() + ": always failed on the initial world");
            return Value::boolean(false);
        }
        for (; actionIndex < actions.size() && ok; ++actionIndex) {
            sim->apply(*actions[actionIndex], loc, onTick);
            if (ok && !check()) {
                ok = false;
                note(loc.str() + ": always failed after action #" + std::to_string(actionIndex) + " " + truncate(print(Value::action(actions[actionIndex]->name, actions[actionIndex]->args)), 160) + " (tick " + std::to_string(sim->ticks()) + ")");
            }
        }
        for (int64_t tick = 0; tick < extraTicks && ok; ++tick) {
            sim->tick();
            if (!check()) {
                ok = false;
                note(loc.str() + ": always failed " + std::to_string(tick + 1) + " ticks after the program (tick " + std::to_string(sim->ticks()) + ")");
            }
        }
        return Value::boolean(ok);
    }
    for (const ActionV* action : actions) sim->apply(*action, loc, [] { return true; });
    const size_t mark = notes.size();
    if (check()) {
        notes.resize(mark);
        return Value::boolean(true);
    }
    for (int64_t tick = 0; tick < extraTicks; ++tick) {
        sim->tick();
        notes.resize(mark);  // only the last check explains the failure
        if (check()) {
            notes.resize(mark);
            return Value::boolean(true);
        }
    }
    note(loc.str() + ": eventually: still false " + std::to_string(extraTicks) + " ticks after the program");
    return Value::boolean(false);
}

Value Interp::eval(const ExprPtr& exprPtr, const EnvPtr& env) {
    const Expr& e = *exprPtr;
    if (++steps > maxSteps) throw BentError(e.loc, "evaluation step budget exhausted (" + std::to_string(maxSteps) + " steps)");
    if ((steps & 0xFFF) == 0 && std::chrono::steady_clock::now() > deadline) throw BentError(e.loc, "case time limit exceeded");
    switch (e.kind) {
    case ExprKind::Lit: return e.literal;
    case ExprKind::Var: {
        if (const Value* local = lookup(env, e.name)) return *local;
        const auto it = prog.globals.find(e.name);
        if (it != prog.globals.end()) {
            const Value& global = it->second;
            if (global.kind == Kind::Func && global.fn && !global.fn->native && !global.fn->closure && global.fn->body) return global;
            return global;
        }
        throw BentError(e.loc, "unknown name '" + e.name + "'");
    }
    case ExprKind::Call: {
        const Value callee = eval(e.args[0], env);
        ValueList args;
        args.reserve(e.args.size() - 1);
        for (size_t index = 1; index < e.args.size(); ++index) args.push_back(eval(e.args[index], env));
        NamedArgs named;
        for (const auto& [key, valueExpr] : e.named) named.emplace_back(key, eval(valueExpr, env));
        return call(callee, args, named, e.loc);
    }
    case ExprKind::List: {
        ValueList items;
        items.reserve(e.args.size());
        bool gen = false;
        for (const ExprPtr& item : e.args) {
            items.push_back(eval(item, env));
            gen = gen || containsGen(items.back());
        }
        if (gen) {
            auto g = std::make_shared<GenV>();
            g->kind = GenKind::ListLit;
            g->values = std::move(items);
            g->loc = e.loc;
            return makeGen(std::move(g));
        }
        return Value::makeList(std::move(items));
    }
    case ExprKind::Record: {
        NamedArgs fields;
        bool gen = false;
        for (const auto& [key, valueExpr] : e.named) {
            fields.emplace_back(key, eval(valueExpr, env));
            gen = gen || containsGen(fields.back().second);
        }
        if (gen) {
            auto g = std::make_shared<GenV>();
            g->kind = GenKind::RecordLit;
            g->named = std::move(fields);
            g->loc = e.loc;
            return makeGen(std::move(g));
        }
        return Value::record(std::move(fields));
    }
    case ExprKind::Field: {
        const Value base = eval(e.args[0], env);
        if (base.kind == Kind::Record) {
            if (const Value* field = base.field(e.name)) return *field;
            std::string names;
            for (const auto& f : base.rec->fields) names += (names.empty() ? "" : ", ") + f.first;
            throw BentError(e.loc, "record has no field '" + e.name + "' (fields: " + names + ")");
        }
        if (base.kind == Kind::Vec3) {
            if (e.name == "x") return Value::number(base.v[0]);
            if (e.name == "y") return Value::number(base.v[1]);
            if (e.name == "z") return Value::number(base.v[2]);
        }
        if (base.kind == Kind::Quat) {
            if (e.name == "w") return Value::number(base.v[0]);
            if (e.name == "x") return Value::number(base.v[1]);
            if (e.name == "y") return Value::number(base.v[2]);
            if (e.name == "z") return Value::number(base.v[3]);
        }
        if (base.kind == Kind::Action) {
            if (e.name == "name") return Value::string(base.act->name);
            if (const Value* arg = base.act->arg(e.name)) return *arg;
            return Value::unit();
        }
        if (base.kind == Kind::Entity && e.name == "ref") return Value::integer(base.i);
        throw BentError(e.loc, std::string("a ") + kindName(base.kind) + " has no field '" + e.name + "'");
    }
    case ExprKind::Index: {
        const Value base = eval(e.args[0], env);
        const Value index = eval(e.args[1], env);
        if (base.kind == Kind::Record && index.kind == Kind::Str) {
            if (const Value* field = base.field(index.text())) return *field;
            throw BentError(e.loc, "record has no field " + quoteString(index.text()));
        }
        const int64_t i = argInt(index, e.loc, "index");
        if (base.kind == Kind::List) {
            const ValueList& items = base.items();
            const int64_t n = static_cast<int64_t>(items.size());
            const int64_t at = i < 0 ? n + i : i;
            if (at < 0 || at >= n) throw BentError(e.loc, "index " + std::to_string(i) + " out of range for a list of " + std::to_string(n));
            return items[static_cast<size_t>(at)];
        }
        if (base.kind == Kind::Str) {
            const int64_t n = static_cast<int64_t>(base.text().size());
            const int64_t at = i < 0 ? n + i : i;
            if (at < 0 || at >= n) throw BentError(e.loc, "index out of range for a string");
            return Value::string(std::string(1, base.text()[static_cast<size_t>(at)]));
        }
        throw BentError(e.loc, std::string("cannot index a ") + kindName(base.kind));
    }
    case ExprKind::Unary: {
        if (e.name == "not") {
            Quiet hush(quiet);
            return Value::boolean(!truthy(eval(e.args[0], env), e.loc));
        }
        const Value operand = eval(e.args[0], env);
        if (operand.kind == Kind::Int) {
            if (operand.i == std::numeric_limits<int64_t>::min()) throw BentError(e.loc, "integer overflow in unary -");
            return Value::integer(-operand.i);
        }
        if (operand.kind == Kind::Float) return Value::number(-operand.f);
        if (operand.kind == Kind::Vec3) return Value::vec3(-operand.v[0], -operand.v[1], -operand.v[2]);
        throw BentError(e.loc, std::string("cannot negate a ") + kindName(operand.kind));
    }
    case ExprKind::Binary: {
        const std::string& op = e.name;
        if (op == "and") {
            if (!truthy(eval(e.args[0], env), e.args[0]->loc)) return Value::boolean(false);
            return Value::boolean(truthy(eval(e.args[1], env), e.args[1]->loc));
        }
        if (op == "or") {
            bool left = false;
            {
                Quiet hush(quiet);
                left = truthy(eval(e.args[0], env), e.args[0]->loc);
            }
            if (left) return Value::boolean(true);
            return Value::boolean(truthy(eval(e.args[1], env), e.args[1]->loc));
        }
        if (op == "==>") {
            bool premise = false;
            {
                Quiet hush(quiet);
                premise = truthy(eval(e.args[0], env), e.args[0]->loc);
            }
            if (!premise) return Value::boolean(true);
            return Value::boolean(truthy(eval(e.args[1], env), e.args[1]->loc));
        }
        const Value a = eval(e.args[0], env);
        const Value b = eval(e.args[1], env);
        if (op == "==" || op == "!=") {
            const bool same = equal(a, b);
            const bool result = op == "==" ? same : !same;
            if (!result) note(e.loc.str() + ": " + brief(a) + " " + op + " " + brief(b) + " is false");
            return Value::boolean(result);
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            const bool result = ordered(op, a, b, e.loc);
            if (!result) note(e.loc.str() + ": " + brief(a) + " " + op + " " + brief(b) + " is false");
            return Value::boolean(result);
        }
        if (op == "++") {
            if (a.kind == Kind::Str && b.kind == Kind::Str) return Value::string(a.text() + b.text());
            if (a.kind == Kind::List && b.kind == Kind::List) {
                ValueList items = a.items();
                items.insert(items.end(), b.items().begin(), b.items().end());
                return Value::makeList(std::move(items));
            }
            typeError(e.loc, op, a, b);
        }
        return arithmetic(op, a, b, e.loc);
    }
    case ExprKind::Approx: {
        const Value a = eval(e.args[0], env);
        const Value b = eval(e.args[1], env);
        const double tolerance = e.args.size() > 2 ? argNumber(eval(e.args[2], env), e.loc, "~= tolerance") : 1.0e-6;
        const bool result = approxEqual(a, b, tolerance);
        if (!result) note(e.loc.str() + ": " + brief(a) + " ~= " + brief(b) + " within " + printFloat(tolerance) + " is false");
        return Value::boolean(result);
    }
    case ExprKind::If: {
        bool condition = false;
        {
            Quiet hush(quiet);
            condition = truthy(eval(e.args[0], env), e.args[0]->loc);
        }
        return condition ? eval(e.args[1], env) : eval(e.args[2], env);
    }
    case ExprKind::Let: return eval(e.args[1], bind(env, e.name, eval(e.args[0], env)));
    case ExprKind::Lambda: {
        auto f = std::make_shared<FuncV>();
        f->name = "lambda@" + std::to_string(e.loc.line) + ":" + std::to_string(e.loc.col);
        f->params = e.params;
        f->body = e.args[0];
        f->closure = env;
        Value value;
        value.kind = Kind::Func;
        value.fn = std::move(f);
        return value;
    }
    case ExprKind::Quant: {
        const Value domain = eval(e.args[0], env);
        const ValueList& items = argList(domain, e.loc, e.flag ? "forall domain" : "exists domain");
        for (const Value& item : items) {
            std::optional<Quiet> hush;
            if (!e.flag) hush.emplace(quiet);
            const bool holds = truthy(eval(e.args[1], bind(env, e.name, item)), e.args[1]->loc);
            hush.reset();
            if (e.flag && !holds) {
                note(e.loc.str() + ": forall " + e.name + " fails for " + e.name + " = " + brief(item));
                return Value::boolean(false);
            }
            if (!e.flag && holds) return Value::boolean(true);
        }
        if (!e.flag) note(e.loc.str() + ": exists " + e.name + ": no element of " + std::to_string(items.size()) + " satisfies it");
        return Value::boolean(e.flag);
    }
    case ExprKind::Temporal: {
        const Value program = eval(e.args[0], env);
        const int64_t ticks = e.args.size() > 2 ? argInt(eval(e.args[2], env), e.loc, "tick count") : 0;
        if (ticks < 0 || ticks > 10000000) throw BentError(e.loc, "tick count out of range");
        const ExprPtr body = e.args[1];
        return runTemporal(e.temporal, program, ticks, [&]() { return eval(body, env); }, e.loc);
    }
    }
    throw BentError(e.loc, "internal: unknown expression");
}

}
