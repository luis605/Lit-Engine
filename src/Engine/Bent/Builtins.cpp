#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "Engine/Bent/Interp.hpp"

namespace bent {

namespace {

using Args = ValueList;

void arity(const Args& args, const NamedArgs& named, size_t lo, size_t hi, const SrcLoc& loc, const char* name) {
    if (!named.empty()) throw BentError(loc, std::string(name) + "() takes no named arguments");
    if (args.size() < lo || args.size() > hi) {
        const std::string expected = lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi);
        throw BentError(loc, std::string(name) + "() takes " + expected + " arguments, got " + std::to_string(args.size()));
    }
}

std::shared_ptr<GenV> newGen(GenKind kind, const SrcLoc& loc) {
    auto gen = std::make_shared<GenV>();
    gen->kind = kind;
    gen->loc = loc;
    return gen;
}

Value vecArg(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind != Kind::Vec3) throw BentError(loc, std::string(what) + ": expected a vec3, got " + kindName(value.kind));
    return value;
}

double vlen(const Value& v) { return std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]); }

Value math1(const Args& args, const NamedArgs& named, const SrcLoc& loc, const char* name, double (*fn)(double)) {
    arity(args, named, 1, 1, loc, name);
    return Value::number(fn(argNumber(args[0], loc, name)));
}

const ValueList& listLike(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Gen && value.gen && value.gen->kind == GenKind::ListLit) return value.gen->values;
    return argList(value, loc, what);
}

const Value& requireFn(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind != Kind::Func) throw BentError(loc, std::string(what) + ": expected a function, got " + kindName(value.kind));
    return value;
}

}

void installBuiltins(Program& p) {
    p.native("int", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "int");
        auto g = newGen(GenKind::Int, loc);
        g->lo = argInt(a[0], loc, "int lo");
        g->hi = argInt(a[1], loc, "int hi");
        if (g->lo > g->hi) throw BentError(loc, "int(lo, hi) needs lo <= hi");
        return makeGen(g);
    });
    p.native("float", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "float");
        auto g = newGen(GenKind::Float, loc);
        g->flo = argNumber(a[0], loc, "float lo");
        g->fhi = argNumber(a[1], loc, "float hi");
        if (!(g->flo <= g->fhi) || !std::isfinite(g->flo) || !std::isfinite(g->fhi)) throw BentError(loc, "float(lo, hi) needs finite lo <= hi (use nasty() for non-finite values)");
        return makeGen(g);
    });
    p.native("bool", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 0, 0, loc, "bool");
        return makeGen(newGen(GenKind::Bool, loc));
    });
    p.native("just", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "just");
        auto g = newGen(GenKind::Just, loc);
        g->values = {a[0]};
        return makeGen(g);
    });
    p.native("elements", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "elements");
        auto g = newGen(GenKind::Elements, loc);
        g->values = listLike(a[0], loc, "elements");
        if (g->values.empty()) throw BentError(loc, "elements([]) has nothing to choose from");
        return makeGen(g);
    });
    p.native("oneof", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        if (!n.empty() || a.empty()) throw BentError(loc, "oneof(g1, g2, ...) needs at least one alternative");
        auto g = newGen(GenKind::OneOf, loc);
        g->values = a.size() == 1 && (a[0].kind == Kind::List || (a[0].kind == Kind::Gen && a[0].gen->kind == GenKind::ListLit)) ? listLike(a[0], loc, "oneof") : a;
        if (g->values.empty()) throw BentError(loc, "oneof([]) has nothing to choose from");
        return makeGen(g);
    });
    p.native("frequency", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "frequency");
        auto g = newGen(GenKind::Frequency, loc);
        for (const Value& pair : listLike(a[0], loc, "frequency")) {
            const ValueList& items = listLike(pair, loc, "frequency entry [weight, generator]");
            if (items.size() != 2) throw BentError(loc, "frequency entries are [weight, generator]");
            const int64_t weight = argInt(items[0], loc, "frequency weight");
            if (weight < 0) throw BentError(loc, "frequency weights must be >= 0");
            g->weights.push_back(static_cast<uint64_t>(weight));
            g->values.push_back(items[1]);
        }
        if (g->values.empty()) throw BentError(loc, "frequency([]) has nothing to choose from");
        return makeGen(g);
    });
    p.native("listOf", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 3, loc, "listOf");
        auto g = newGen(GenKind::ListOf, loc);
        g->values = {a[0]};
        g->lo = a.size() > 1 ? argInt(a[1], loc, "listOf min") : 0;
        g->hi = a.size() > 2 ? argInt(a[2], loc, "listOf max") : std::max<int64_t>(g->lo, 10);
        if (g->lo < 0 || g->lo > g->hi || g->hi > 100000) throw BentError(loc, "listOf(g, min, max) needs 0 <= min <= max <= 100000");
        return makeGen(g);
    });
    p.native("unitVec3", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 0, 0, loc, "unitVec3");
        return makeGen(newGen(GenKind::UnitVec, loc));
    });
    p.native("unitQuat", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 0, 0, loc, "unitQuat");
        return makeGen(newGen(GenKind::Rotation, loc));
    });
    p.native("nasty", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "nasty");
        auto g = newGen(GenKind::Nasty, loc);
        g->values = {a[0]};
        return makeGen(g);
    });
    p.native("text", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 0, 2, loc, "text");
        auto g = newGen(GenKind::Text, loc);
        g->lo = a.size() > 0 ? argInt(a[0], loc, "text min") : 0;
        g->hi = a.size() > 1 ? argInt(a[1], loc, "text max") : std::max<int64_t>(g->lo, 12);
        if (g->lo < 0 || g->lo > g->hi) throw BentError(loc, "text(min, max) needs 0 <= min <= max");
        return makeGen(g);
    });
    p.native("ident", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 0, 2, loc, "ident");
        auto g = newGen(GenKind::Ident, loc);
        g->lo = a.size() > 0 ? argInt(a[0], loc, "ident min") : 1;
        g->hi = a.size() > 1 ? argInt(a[1], loc, "ident max") : std::max<int64_t>(g->lo, 8);
        if (g->lo < 1 || g->lo > g->hi) throw BentError(loc, "ident(min, max) needs 1 <= min <= max");
        return makeGen(g);
    });
    p.native("bind", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "bind");
        auto g = newGen(GenKind::Bind, loc);
        g->values = {a[0]};
        g->fn = requireFn(a[1], loc, "bind");
        return makeGen(g);
    });
    p.native("delay", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "delay");
        auto g = newGen(GenKind::Delay, loc);
        g->fn = requireFn(a[0], loc, "delay");
        return makeGen(g);
    });
    p.native("subseq", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "subseq");
        auto g = newGen(GenKind::Subseq, loc);
        g->values = argList(a[0], loc, "subseq");
        return makeGen(g);
    });
    p.native("sample", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "sample");
        return in.realize(a[0], loc);
    });

    p.native("map", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "map");
        requireFn(a[1], loc, "map");
        if (a[0].kind == Kind::Gen) {
            auto g = newGen(GenKind::Map, loc);
            g->values = {a[0]};
            g->fn = a[1];
            return makeGen(g);
        }
        ValueList out;
        for (const Value& item : argList(a[0], loc, "map")) out.push_back(in.call(a[1], {item}, {}, loc));
        return Value::makeList(std::move(out));
    });
    p.native("filter", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "filter");
        requireFn(a[1], loc, "filter");
        if (a[0].kind == Kind::Gen) {
            auto g = newGen(GenKind::Filter, loc);
            g->values = {a[0]};
            g->fn = a[1];
            return makeGen(g);
        }
        ValueList out;
        for (const Value& item : argList(a[0], loc, "filter")) {
            if (truthy(in.call(a[1], {item}, {}, loc), loc)) out.push_back(item);
        }
        return Value::makeList(std::move(out));
    });

    p.native("len", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "len");
        if (a[0].kind == Kind::Str) return Value::integer(static_cast<int64_t>(a[0].text().size()));
        if (a[0].kind == Kind::Record) return Value::integer(static_cast<int64_t>(a[0].rec->fields.size()));
        return Value::integer(static_cast<int64_t>(argList(a[0], loc, "len").size()));
    });
    p.native("range", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 2, loc, "range");
        const int64_t lo = a.size() == 2 ? argInt(a[0], loc, "range") : 0;
        const int64_t hi = argInt(a.back(), loc, "range");
        if (hi - lo > 10000000) throw BentError(loc, "range too large");
        ValueList out;
        for (int64_t value = lo; value < hi; ++value) out.push_back(Value::integer(value));
        return Value::makeList(std::move(out));
    });
    p.native("fold", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 3, 3, loc, "fold");
        Value acc = a[1];
        for (const Value& item : argList(a[0], loc, "fold")) acc = in.call(a[2], {acc, item}, {}, loc);
        return acc;
    });
    p.native("all", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 2, loc, "all");
        for (const Value& item : argList(a[0], loc, "all")) {
            if (!truthy(a.size() == 2 ? in.call(a[1], {item}, {}, loc) : item, loc)) return Value::boolean(false);
        }
        return Value::boolean(true);
    });
    p.native("any", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 2, loc, "any");
        for (const Value& item : argList(a[0], loc, "any")) {
            if (truthy(a.size() == 2 ? in.call(a[1], {item}, {}, loc) : item, loc)) return Value::boolean(true);
        }
        return Value::boolean(false);
    });
    p.native("count", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "count");
        int64_t total = 0;
        for (const Value& item : argList(a[0], loc, "count")) total += truthy(in.call(a[1], {item}, {}, loc), loc) ? 1 : 0;
        return Value::integer(total);
    });
    p.native("head", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "head");
        const ValueList& items = argList(a[0], loc, "head");
        if (items.empty()) throw BentError(loc, "head of an empty list");
        return items.front();
    });
    p.native("last", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "last");
        const ValueList& items = argList(a[0], loc, "last");
        if (items.empty()) throw BentError(loc, "last of an empty list");
        return items.back();
    });
    p.native("tail", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "tail");
        const ValueList& items = argList(a[0], loc, "tail");
        if (items.empty()) return Value::makeList({});
        return Value::makeList(ValueList(items.begin() + 1, items.end()));
    });
    p.native("take", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "take");
        const ValueList& items = argList(a[0], loc, "take");
        const size_t count = static_cast<size_t>(std::clamp<int64_t>(argInt(a[1], loc, "take"), 0, static_cast<int64_t>(items.size())));
        return Value::makeList(ValueList(items.begin(), items.begin() + static_cast<long>(count)));
    });
    p.native("drop", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "drop");
        const ValueList& items = argList(a[0], loc, "drop");
        const size_t count = static_cast<size_t>(std::clamp<int64_t>(argInt(a[1], loc, "drop"), 0, static_cast<int64_t>(items.size())));
        return Value::makeList(ValueList(items.begin() + static_cast<long>(count), items.end()));
    });
    p.native("reverse", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "reverse");
        ValueList items = argList(a[0], loc, "reverse");
        std::reverse(items.begin(), items.end());
        return Value::makeList(std::move(items));
    });
    p.native("concat", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        if (!n.empty()) throw BentError(loc, "concat() takes no named arguments");
        ValueList out;
        for (const Value& part : a.size() == 1 ? argList(a[0], loc, "concat") : a) {
            const ValueList& items = argList(part, loc, "concat");
            out.insert(out.end(), items.begin(), items.end());
        }
        return Value::makeList(std::move(out));
    });
    p.native("repeat", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "repeat");
        const int64_t count = argInt(a[1], loc, "repeat");
        if (count < 0 || count > 1000000) throw BentError(loc, "repeat count out of range");
        return Value::makeList(ValueList(static_cast<size_t>(count), a[0]));
    });
    p.native("zip", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "zip");
        const ValueList& x = argList(a[0], loc, "zip");
        const ValueList& y = argList(a[1], loc, "zip");
        ValueList out;
        for (size_t index = 0; index < std::min(x.size(), y.size()); ++index) out.push_back(Value::makeList({x[index], y[index]}));
        return Value::makeList(std::move(out));
    });
    p.native("enumerate", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "enumerate");
        ValueList out;
        int64_t index = 0;
        for (const Value& item : argList(a[0], loc, "enumerate")) out.push_back(Value::makeList({Value::integer(index++), item}));
        return Value::makeList(std::move(out));
    });
    p.native("contains", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "contains");
        if (a[0].kind == Kind::Str) return Value::boolean(a[0].text().find(argString(a[1], loc, "contains")) != std::string::npos);
        for (const Value& item : argList(a[0], loc, "contains")) {
            if (equal(item, a[1])) return Value::boolean(true);
        }
        return Value::boolean(false);
    });
    p.native("indexOf", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "indexOf");
        const ValueList& items = argList(a[0], loc, "indexOf");
        for (size_t index = 0; index < items.size(); ++index) {
            if (equal(items[index], a[1])) return Value::integer(static_cast<int64_t>(index));
        }
        return Value::integer(-1);
    });
    p.native("sort", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "sort");
        ValueList items = argList(a[0], loc, "sort");
        std::stable_sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return compare(x, y) < 0; });
        return Value::makeList(std::move(items));
    });
    p.native("sortBy", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "sortBy");
        std::vector<std::pair<Value, Value>> keyed;
        for (const Value& item : argList(a[0], loc, "sortBy")) keyed.emplace_back(in.call(a[1], {item}, {}, loc), item);
        std::stable_sort(keyed.begin(), keyed.end(), [](const auto& x, const auto& y) { return compare(x.first, y.first) < 0; });
        ValueList out;
        for (auto& entry : keyed) out.push_back(std::move(entry.second));
        return Value::makeList(std::move(out));
    });
    p.native("dedup", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "dedup");
        ValueList out;
        for (const Value& item : argList(a[0], loc, "dedup")) {
            if (std::none_of(out.begin(), out.end(), [&](const Value& seen) { return equal(seen, item); })) out.push_back(item);
        }
        return Value::makeList(std::move(out));
    });
    p.native("allDistinct", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "allDistinct");
        ValueList items = argList(a[0], loc, "allDistinct");
        std::sort(items.begin(), items.end(), [](const Value& x, const Value& y) { return compare(x, y) < 0; });
        for (size_t index = 1; index < items.size(); ++index) {
            if (equal(items[index - 1], items[index])) return Value::boolean(false);
        }
        return Value::boolean(true);
    });
    p.native("flatten", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "flatten");
        ValueList out;
        for (const Value& item : argList(a[0], loc, "flatten")) {
            if (item.kind == Kind::List) out.insert(out.end(), item.items().begin(), item.items().end());
            else out.push_back(item);
        }
        return Value::makeList(std::move(out));
    });
    const auto extreme = [](bool wantMax) {
        return [wantMax](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
            if (!n.empty()) throw BentError(loc, "min/max take no named arguments");
            const ValueList& items = a.size() == 1 ? argList(a[0], loc, wantMax ? "max" : "min") : a;
            if (items.empty()) throw BentError(loc, std::string(wantMax ? "max" : "min") + " of nothing");
            Value best = items[0];
            for (const Value& item : items) {
                const int c = compare(item, best);
                if (wantMax ? c > 0 : c < 0) best = item;
            }
            return best;
        };
    };
    p.native("min", extreme(false));
    p.native("max", extreme(true));
    p.native("sum", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "sum");
        bool allInt = true;
        int64_t isum = 0;
        double fsum = 0.0;
        Value vsum = Value::vec3(0, 0, 0);
        bool vec = false;
        for (const Value& item : argList(a[0], loc, "sum")) {
            if (item.kind == Kind::Vec3) {
                vec = true;
                for (int k = 0; k < 3; ++k) vsum.v[k] += item.v[k];
                continue;
            }
            const double x = argNumber(item, loc, "sum");
            if (item.kind == Kind::Int && allInt && !__builtin_add_overflow(isum, item.i, &isum)) {
            } else {
                allInt = false;
            }
            fsum += x;
        }
        if (vec) return vsum;
        return allInt ? Value::integer(isum) : Value::number(fsum);
    });

    p.native("keys", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "keys");
        if (a[0].kind != Kind::Record) throw BentError(loc, "keys() needs a record");
        ValueList out;
        for (const auto& field : a[0].rec->fields) out.push_back(Value::string(field.first));
        return Value::makeList(std::move(out));
    });
    p.native("values", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "values");
        if (a[0].kind != Kind::Record) throw BentError(loc, "values() needs a record");
        ValueList out;
        for (const auto& field : a[0].rec->fields) out.push_back(field.second);
        return Value::makeList(std::move(out));
    });
    p.native("has", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "has");
        return Value::boolean(a[0].field(argString(a[1], loc, "has")) != nullptr);
    });
    p.native("get", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 3, loc, "get");
        if (const Value* field = a[0].field(argString(a[1], loc, "get"))) return *field;
        if (a.size() == 3) return a[2];
        throw BentError(loc, "get: no field " + quoteString(a[1].text()));
    });
    p.native("fromPairs", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "fromPairs");
        NamedArgs fields;
        for (const Value& pair : argList(a[0], loc, "fromPairs")) {
            const ValueList& kv = argList(pair, loc, "fromPairs entry [key, value]");
            if (kv.size() != 2) throw BentError(loc, "fromPairs entries are [key, value]");
            fields.emplace_back(argString(kv[0], loc, "fromPairs key"), kv[1]);
        }
        return Value::record(std::move(fields));
    });
    p.native("substr", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 3, loc, "substr");
        const std::string& text = argString(a[0], loc, "substr");
        const int64_t size = static_cast<int64_t>(text.size());
        const int64_t start = std::clamp<int64_t>(argInt(a[1], loc, "substr start"), 0, size);
        const int64_t count = a.size() == 3 ? std::clamp<int64_t>(argInt(a[2], loc, "substr count"), 0, size - start) : size - start;
        return Value::string(text.substr(static_cast<size_t>(start), static_cast<size_t>(count)));
    });
    p.native("firstDiff", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "firstDiff");
        const std::string& x = argString(a[0], loc, "firstDiff");
        const std::string& y = argString(a[1], loc, "firstDiff");
        size_t line = 1;
        size_t start = 0;
        for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
            const char cx = i < x.size() ? x[i] : '\0';
            const char cy = i < y.size() ? y[i] : '\0';
            if (cx != cy) {
                const auto lineOf = [&](const std::string& s) { const size_t end = s.find('\n', start); return s.substr(std::min(start, s.size()), end == std::string::npos ? std::string::npos : end - start); };
                return Value::string("line " + std::to_string(line) + ": " + lineOf(x) + "  <>  " + lineOf(y));
            }
            if (cx == '\n') {
                ++line;
                start = i + 1;
            }
        }
        return Value::string("");
    });
    p.native("chars", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "chars");
        ValueList out;
        for (const char c : argString(a[0], loc, "chars")) out.push_back(Value::string(std::string(1, c)));
        return Value::makeList(std::move(out));
    });
    p.native("with", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        if (a.size() != 1 || a[0].kind != Kind::Record) throw BentError(loc, "with(record, field: value, ...) needs a record");
        NamedArgs fields = a[0].rec->fields;
        fields.insert(fields.end(), n.begin(), n.end());
        return Value::record(std::move(fields));
    });

    p.native("abs", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "abs");
        if (a[0].kind == Kind::Int) return Value::integer(a[0].i < 0 ? -a[0].i : a[0].i);
        return Value::number(std::fabs(argNumber(a[0], loc, "abs")));
    });
    p.native("sqrt", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "sqrt", [](double x) { return std::sqrt(x); }); });
    p.native("floor", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "floor", [](double x) { return std::floor(x); }); });
    p.native("ceil", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "ceil", [](double x) { return std::ceil(x); }); });
    p.native("round", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "round", [](double x) { return std::round(x); }); });
    p.native("sin", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "sin", [](double x) { return std::sin(x); }); });
    p.native("cos", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "cos", [](double x) { return std::cos(x); }); });
    p.native("exp", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "exp", [](double x) { return std::exp(x); }); });
    p.native("log", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "log", [](double x) { return std::log(x); }); });
    p.native("radians", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) { return math1(a, n, loc, "radians", [](double x) { return x * 3.14159265358979323846 / 180.0; }); });
    p.native("atan2", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "atan2");
        return Value::number(std::atan2(argNumber(a[0], loc, "atan2"), argNumber(a[1], loc, "atan2")));
    });
    p.native("pow", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "pow");
        return Value::number(std::pow(argNumber(a[0], loc, "pow"), argNumber(a[1], loc, "pow")));
    });
    p.native("clamp", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 3, 3, loc, "clamp");
        const double x = argNumber(a[0], loc, "clamp");
        const double lo = argNumber(a[1], loc, "clamp");
        const double hi = argNumber(a[2], loc, "clamp");
        if (a[0].kind == Kind::Int && a[1].kind == Kind::Int && a[2].kind == Kind::Int) return Value::integer(std::clamp(a[0].i, a[1].i, std::max(a[1].i, a[2].i)));
        return Value::number(std::clamp(x, lo, std::max(lo, hi)));
    });
    p.native("isNaN", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "isNaN");
        if (a[0].kind == Kind::Vec3 || a[0].kind == Kind::Quat) return Value::boolean(std::isnan(a[0].v[0]) || std::isnan(a[0].v[1]) || std::isnan(a[0].v[2]) || std::isnan(a[0].v[3]));
        return Value::boolean(std::isnan(argNumber(a[0], loc, "isNaN")));
    });
    p.native("isFinite", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "isFinite");
        if (a[0].kind == Kind::Vec3 || a[0].kind == Kind::Quat) return Value::boolean(std::isfinite(a[0].v[0]) && std::isfinite(a[0].v[1]) && std::isfinite(a[0].v[2]) && std::isfinite(a[0].v[3]));
        return Value::boolean(std::isfinite(argNumber(a[0], loc, "isFinite")));
    });
    p.native("toInt", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "toInt");
        const double x = argNumber(a[0], loc, "toInt");
        if (!std::isfinite(x) || std::fabs(x) > 9.0e18) throw BentError(loc, "toInt of " + printFloat(x));
        return Value::integer(static_cast<int64_t>(x));
    });
    p.native("toFloat", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "toFloat");
        return Value::number(argNumber(a[0], loc, "toFloat"));
    });
    p.native("vec3", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 3, loc, "vec3");
        if (a.size() == 1) {
            const double s = argNumber(a[0], loc, "vec3");
            return Value::vec3(s, s, s);
        }
        if (a.size() != 3) throw BentError(loc, "vec3(x, y, z) or vec3(s)");
        return Value::vec3(argNumber(a[0], loc, "vec3 x"), argNumber(a[1], loc, "vec3 y"), argNumber(a[2], loc, "vec3 z"));
    }, true);
    p.native("quat", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 4, 4, loc, "quat");
        return Value::quat(argNumber(a[0], loc, "quat w"), argNumber(a[1], loc, "quat x"), argNumber(a[2], loc, "quat y"), argNumber(a[3], loc, "quat z"));
    }, true);
    p.native("axisAngle", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "axisAngle");
        const Value axis = vecArg(a[0], loc, "axisAngle axis");
        const double length = vlen(axis);
        const double half = argNumber(a[1], loc, "axisAngle degrees") * 3.14159265358979323846 / 360.0;
        if (!(length > 0.0)) return Value::quat(1, 0, 0, 0);
        const double s = std::sin(half) / length;
        return Value::quat(std::cos(half), axis.v[0] * s, axis.v[1] * s, axis.v[2] * s);
    }, true);
    p.native("entity", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "entity");
        return Value::entity(argInt(a[0], loc, "entity"));
    }, true);
    p.native("dot", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "dot");
        const Value x = vecArg(a[0], loc, "dot");
        const Value y = vecArg(a[1], loc, "dot");
        return Value::number(x.v[0] * y.v[0] + x.v[1] * y.v[1] + x.v[2] * y.v[2]);
    });
    p.native("cross", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "cross");
        const Value x = vecArg(a[0], loc, "cross");
        const Value y = vecArg(a[1], loc, "cross");
        return Value::vec3(x.v[1] * y.v[2] - x.v[2] * y.v[1], x.v[2] * y.v[0] - x.v[0] * y.v[2], x.v[0] * y.v[1] - x.v[1] * y.v[0]);
    });
    p.native("length", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "length");
        if (a[0].kind == Kind::Quat) return Value::number(std::sqrt(a[0].v[0] * a[0].v[0] + a[0].v[1] * a[0].v[1] + a[0].v[2] * a[0].v[2] + a[0].v[3] * a[0].v[3]));
        return Value::number(vlen(vecArg(a[0], loc, "length")));
    });
    p.native("distance", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "distance");
        const Value x = vecArg(a[0], loc, "distance");
        const Value y = vecArg(a[1], loc, "distance");
        return Value::number(vlen(Value::vec3(x.v[0] - y.v[0], x.v[1] - y.v[1], x.v[2] - y.v[2])));
    });
    p.native("normalize", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "normalize");
        if (a[0].kind == Kind::Quat) {
            const double l = std::sqrt(a[0].v[0] * a[0].v[0] + a[0].v[1] * a[0].v[1] + a[0].v[2] * a[0].v[2] + a[0].v[3] * a[0].v[3]);
            return Value::quat(a[0].v[0] / l, a[0].v[1] / l, a[0].v[2] / l, a[0].v[3] / l);
        }
        const Value x = vecArg(a[0], loc, "normalize");
        const double l = vlen(x);
        return Value::vec3(x.v[0] / l, x.v[1] / l, x.v[2] / l);
    });
    p.native("lerp", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 3, 3, loc, "lerp");
        const double t = argNumber(a[2], loc, "lerp t");
        if (a[0].kind == Kind::Vec3 && a[1].kind == Kind::Vec3) return Value::vec3(a[0].v[0] + (a[1].v[0] - a[0].v[0]) * t, a[0].v[1] + (a[1].v[1] - a[0].v[1]) * t, a[0].v[2] + (a[1].v[2] - a[0].v[2]) * t);
        const double x = argNumber(a[0], loc, "lerp");
        return Value::number(x + (argNumber(a[1], loc, "lerp") - x) * t);
    });
    p.native("rotate", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "rotate");
        if (a[0].kind != Kind::Quat) throw BentError(loc, "rotate(q, v) needs a quat");
        (void)in;
        const double w = a[0].v[0], qx = a[0].v[1], qy = a[0].v[2], qz = a[0].v[3];
        const Value v = vecArg(a[1], loc, "rotate");
        const double tx = 2.0 * (qy * v.v[2] - qz * v.v[1]), ty = 2.0 * (qz * v.v[0] - qx * v.v[2]), tz = 2.0 * (qx * v.v[1] - qy * v.v[0]);
        return Value::vec3(v.v[0] + w * tx + (qy * tz - qz * ty), v.v[1] + w * ty + (qz * tx - qx * tz), v.v[2] + w * tz + (qx * ty - qy * tx));
    });

    p.native("str", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "str");
        return a[0].kind == Kind::Str ? a[0] : Value::string(print(a[0]));
    });
    p.native("join", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 2, loc, "join");
        std::string out;
        const std::string sep = a.size() == 2 ? argString(a[1], loc, "join separator") : std::string();
        bool first = true;
        for (const Value& item : argList(a[0], loc, "join")) {
            if (!first) out += sep;
            first = false;
            out += item.kind == Kind::Str ? item.text() : print(item);
        }
        return Value::string(std::move(out));
    });
    p.native("kindOf", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "kindOf");
        return Value::string(kindName(a[0].kind));
    });
    p.native("hash", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "hash");
        return Value::integer(static_cast<int64_t>(hashValue(a[0]) & 0x7fffffffffffffffULL));
    });
    p.native("error", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) -> Value {
        arity(a, n, 1, 1, loc, "error");
        throw BentError(loc, a[0].kind == Kind::Str ? a[0].text() : print(a[0]));
    });
    p.native("trace", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "trace");
        in.note(loc.str() + ": trace " + (a[0].kind == Kind::Str ? a[0].text() : print(a[0])) + " = " + truncate(print(a[1]), 400));
        return a[1];
    });
    p.native("cover", [](Interp& in, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 2, loc, "cover");
        in.covers.emplace_back(argString(a[0], loc, "cover label"), truthy(a[1], loc));
        return Value::boolean(true);
    });
    p.native("approx", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 2, 3, loc, "approx");
        return Value::boolean(approxEqual(a[0], a[1], a.size() == 3 ? argNumber(a[2], loc, "approx tolerance") : 1.0e-6));
    });
    p.native("actionName", [](Interp&, const Args& a, const NamedArgs& n, const SrcLoc& loc) {
        arity(a, n, 1, 1, loc, "actionName");
        if (a[0].kind != Kind::Action) throw BentError(loc, "actionName needs an action");
        return Value::string(a[0].act->name);
    });
}

}
