module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;

namespace {
double toNumber(const Json& j) {
    if (j.isBool()) return j.asBool() ? 1.0 : 0.0;
    return j.asNumber();
}

std::string fmt(double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.4g", v);
    return buf;
}
}

const Json* Blackboard::get(const std::string& key) const {
    const auto it = m_values.find(key);
    return it == m_values.end() ? nullptr : &it->second;
}

double Blackboard::number(const std::string& key, double fallback) const {
    const Json* j = get(key);
    return j && (j->isNumber() || j->isBool()) ? toNumber(*j) : fallback;
}

bool Blackboard::boolean(const std::string& key, bool fallback) const {
    const Json* j = get(key);
    if (!j) return fallback;
    if (j->isBool()) return j->asBool();
    if (j->isNumber()) return j->asNumber() != 0.0;
    return fallback;
}

std::string Blackboard::string(const std::string& key, const std::string& fallback) const {
    const Json* j = get(key);
    return j && j->isString() ? j->asString() : fallback;
}

void Blackboard::importParams(const ParamBus& bus, const std::string& prefix) {
    for (const ParamInfo& p : bus.params()) m_values[prefix + p.name] = Json(static_cast<double>(p.value));
}

Json Blackboard::toJson() const {
    Json out = Json::object();
    for (const auto& [k, v] : m_values) out.set(k, v);
    return out;
}

void DecisionTrace::record(TraceEvent event) {
    if (!m_enabled) return;
    if (m_capacity > 0 && m_events.size() >= m_capacity) m_events.erase(m_events.begin(), m_events.begin() + static_cast<std::ptrdiff_t>(m_events.size() - m_capacity + 1));
    m_events.push_back(std::move(event));
}

std::vector<const TraceEvent*> DecisionTrace::forTick(uint64_t tick) const {
    std::vector<const TraceEvent*> out;
    for (const TraceEvent& e : m_events) {
        if (e.tick == tick) out.push_back(&e);
    }
    return out;
}

std::string DecisionTrace::lastWinner(const std::string& node) const {
    for (auto it = m_events.rbegin(); it != m_events.rend(); ++it) {
        if (it->node == node) return it->winner;
    }
    return {};
}

Json DecisionTrace::toJson() const {
    Json events = Json::array();
    for (const TraceEvent& e : m_events) {
        Json je = Json::object();
        je.set("tick", e.tick);
        je.set("agent", e.agent);
        je.set("node", e.node);
        je.set("kind", e.kind);
        je.set("winner", e.winner);
        je.set("reason", e.reason);
        Json options = Json::array();
        for (const TraceOption& o : e.options) {
            Json jo = Json::object();
            jo.set("name", o.name);
            jo.set("score", o.score);
            jo.set("available", o.available);
            Json cs = Json::array();
            for (const TraceConsideration& c : o.considerations) {
                Json jc = Json::object();
                jc.set("input", c.input);
                jc.set("value", c.value);
                jc.set("score", c.score);
                cs.push(std::move(jc));
            }
            jo.set("considerations", std::move(cs));
            options.push(std::move(jo));
        }
        je.set("options", std::move(options));
        events.push(std::move(je));
    }
    Json root = Json::object();
    root.set("events", std::move(events));
    return root;
}

std::string DecisionTrace::toText() const {
    std::string out;
    for (const TraceEvent& e : m_events) {
        out += "tick " + std::to_string(e.tick) + " agent " + std::to_string(e.agent) + " " + e.node + " [" + e.kind + "] -> " + e.winner + " (" + e.reason + ")\n";
        for (const TraceOption& o : e.options) {
            out += std::string("  ") + (o.name == e.winner ? "* " : "  ") + o.name + " " + (o.available ? fmt(o.score) : std::string("unavailable"));
            for (const TraceConsideration& c : o.considerations) out += " " + c.input + "=" + fmt(c.value) + "->" + fmt(c.score);
            out += "\n";
        }
    }
    return out;
}

bool DecisionTrace::writeFile(const std::string& path, bool asText) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << (asText ? toText() : toJson().dump(2));
    return static_cast<bool>(out);
}

namespace {
enum class Kind { Sequence, Selector, Parallel, Inverter, Succeeder, Failer, Repeat, Wait, Condition, Set, Action, Utility, Fsm };

struct Cond {
    enum class T { Leaf, All, Any, Not, Always } type = T::Always;
    std::string key;
    std::string op;
    Json value;
    std::string valueKey;
    std::vector<Cond> kids;
};

struct Consideration {
    std::string input;
    float minimum = 0.0f;
    float maximum = 1.0f;
    ParamCurve curve;
};

struct Option {
    std::string name;
    int child = -1;
    float weight = 1.0f;
    std::vector<Consideration> considerations;
    bool hasGate = false;
    Cond gate;
};

struct Transition {
    std::string to;
    Cond when;
    float after = -1.0f;
};

struct State {
    std::string name;
    int child = -1;
    std::vector<Transition> transitions;
};

struct Node {
    Kind kind = Kind::Sequence;
    std::string name;
    std::vector<int> children;
    std::string key;
    std::string action;
    Json value;
    std::string valueKey;
    bool add = false;
    int count = -1;
    float seconds = 0.0f;
    bool reactive = false;
    bool parallelAny = false;
    Cond cond;
    std::vector<Option> options;
    bool weighted = false;
    float power = 1.0f;
    float stickiness = 0.0f;
    float minScore = 0.0f;
    bool reconsider = true;
    std::string combine = "product";
    std::vector<State> states;
    std::vector<Transition> global;
    std::string initial;
};

struct NodeState {
    int index = 0;
    int last = -1;
    int counter = 0;
    float timer = 0.0f;
    float stateTime = 0.0f;
};
}

struct BehaviorGraph::Impl {
    std::string name;
    std::vector<Node> nodes;
    Json initial = Json::object();
    int root = -1;
};

namespace {
struct Parser {
    BehaviorGraph::Impl& g;
    std::string error;

    bool fail(const std::string& path, const std::string& what) {
        if (error.empty()) error = path + ": " + what;
        return false;
    }

    bool cond(const Json& j, Cond& out, const std::string& path) {
        if (!j.isObject()) return fail(path, "condition must be object");
        if (const Json* all = j.get("all")) {
            out.type = Cond::T::All;
            for (const Json& k : all->items()) {
                out.kids.emplace_back();
                if (!cond(k, out.kids.back(), path + ".all")) return false;
            }
            return true;
        }
        if (const Json* any = j.get("any")) {
            out.type = Cond::T::Any;
            for (const Json& k : any->items()) {
                out.kids.emplace_back();
                if (!cond(k, out.kids.back(), path + ".any")) return false;
            }
            return true;
        }
        if (const Json* n = j.get("not")) {
            out.type = Cond::T::Not;
            out.kids.emplace_back();
            return cond(*n, out.kids.back(), path + ".not");
        }
        const Json* key = j.get("key");
        if (!key || !key->isString()) return fail(path, "condition needs key");
        out.type = Cond::T::Leaf;
        out.key = key->asString();
        if (const Json* op = j.get("op")) {
            out.op = op->asString();
            static const char* ok[] = {"<", "<=", ">", ">=", "==", "!="};
            if (std::find_if(std::begin(ok), std::end(ok), [&](const char* s) { return out.op == s; }) == std::end(ok)) return fail(path, "bad op " + out.op);
        }
        if (const Json* v = j.get("value")) {
            if (v->isObject() && v->get("key")) out.valueKey = v->get("key")->asString();
            else out.value = *v;
        }
        return true;
    }

    bool consideration(const Json& j, Consideration& c, const std::string& path) {
        const Json* in = j.get("input");
        if (!in || !in->isString()) return fail(path, "consideration needs input");
        c.input = in->asString();
        c.minimum = static_cast<float>(j.get("min") ? j.get("min")->asNumber() : 0.0);
        c.maximum = static_cast<float>(j.get("max") ? j.get("max")->asNumber() : 1.0);
        if (c.maximum <= c.minimum) return fail(path, "max must exceed min");
        c.curve.points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        if (const Json* curve = j.get("curve")) {
            c.curve.points.clear();
            for (const Json& p : curve->items()) {
                const Json* x = p.at(0);
                const Json* y = p.at(1);
                if (!x || !y) return fail(path, "curve point needs [x,y]");
                const float fx = static_cast<float>(x->asNumber());
                if (!c.curve.points.empty() && fx <= c.curve.points.back().first) return fail(path, "curve x must increase");
                c.curve.points.emplace_back(fx, static_cast<float>(y->asNumber()));
            }
            if (c.curve.points.empty()) return fail(path, "empty curve");
        }
        if (const Json* mode = j.get("mode")) {
            if (mode->asString() == "smooth") c.curve.mode = CurveMode::Smooth;
            else if (mode->asString() == "step") c.curve.mode = CurveMode::Step;
            else if (mode->asString() != "linear") return fail(path, "bad curve mode");
        }
        return true;
    }

    bool transition(const Json& j, Transition& t, const std::string& path) {
        const Json* to = j.get("to");
        if (!to || !to->isString()) return fail(path, "transition needs to");
        t.to = to->asString();
        if (const Json* w = j.get("when")) {
            if (!cond(*w, t.when, path + ".when")) return false;
        }
        if (const Json* a = j.get("after")) t.after = static_cast<float>(a->asNumber());
        if (!j.get("when") && !j.get("after")) return fail(path, "transition needs when or after");
        return true;
    }

    int node(const Json& j, const std::string& path) {
        if (!j.isObject()) return fail(path, "node must be object"), -1;
        const Json* type = j.get("type");
        if (!type || !type->isString()) return fail(path, "node needs type"), -1;
        const int id = static_cast<int>(g.nodes.size());
        g.nodes.emplace_back();
        const std::string t = type->asString();
        std::string name = j.get("name") ? j.get("name")->asString() : t + "#" + std::to_string(id);
        const auto setKind = [&](Kind k) { g.nodes[id].kind = k; };
        static const std::map<std::string, Kind> kinds = {{"sequence", Kind::Sequence}, {"selector", Kind::Selector}, {"parallel", Kind::Parallel}, {"inverter", Kind::Inverter},
                                                          {"succeeder", Kind::Succeeder}, {"failer", Kind::Failer}, {"repeat", Kind::Repeat}, {"wait", Kind::Wait},
                                                          {"condition", Kind::Condition}, {"set", Kind::Set}, {"action", Kind::Action}, {"utility", Kind::Utility}, {"fsm", Kind::Fsm}};
        const auto kit = kinds.find(t);
        if (kit == kinds.end()) return fail(path, "unknown node type " + t), -1;
        setKind(kit->second);
        g.nodes[id].name = name;
        const Kind kind = kit->second;
        const auto childList = [&](const char* field, std::vector<int>& out) {
            const Json* arr = j.get(field);
            if (!arr || !arr->isArray()) return fail(path, std::string("needs ") + field + " array");
            int i = 0;
            for (const Json& c : arr->items()) {
                const int cid = node(c, path + "." + name + "[" + std::to_string(i++) + "]");
                if (cid < 0) return false;
                out.push_back(cid);
            }
            return true;
        };
        const auto single = [&]() {
            const Json* c = j.get("child");
            if (!c) return fail(path, "needs child");
            const int cid = node(*c, path + "." + name);
            if (cid < 0) return false;
            g.nodes[id].children.push_back(cid);
            return true;
        };
        std::vector<int> kids;
        switch (kind) {
        case Kind::Sequence:
        case Kind::Selector:
            if (!childList("children", kids)) return -1;
            g.nodes[id].children = kids;
            g.nodes[id].reactive = j.get("reactive") && j.get("reactive")->asBool();
            break;
        case Kind::Parallel:
            if (!childList("children", kids)) return -1;
            g.nodes[id].children = kids;
            g.nodes[id].parallelAny = j.get("policy") && j.get("policy")->asString() == "any";
            break;
        case Kind::Inverter:
        case Kind::Succeeder:
        case Kind::Failer:
            if (!single()) return -1;
            break;
        case Kind::Repeat:
            if (!single()) return -1;
            g.nodes[id].count = j.get("count") ? static_cast<int>(j.get("count")->asNumber()) : -1;
            break;
        case Kind::Wait:
            g.nodes[id].seconds = static_cast<float>(j.get("seconds") ? j.get("seconds")->asNumber() : 0.0);
            break;
        case Kind::Condition: {
            const Json* when = j.get("when");
            if (!when || !cond(*when, g.nodes[id].cond, path + "." + name)) return fail(path, "condition node needs when"), -1;
            break;
        }
        case Kind::Set: {
            const Json* key = j.get("key");
            if (!key || !key->isString()) return fail(path, "set needs key"), -1;
            g.nodes[id].key = key->asString();
            if (const Json* add = j.get("add")) {
                g.nodes[id].add = true;
                g.nodes[id].value = *add;
            } else if (const Json* v = j.get("value")) {
                if (v->isObject() && v->get("key")) g.nodes[id].valueKey = v->get("key")->asString();
                else g.nodes[id].value = *v;
            } else return fail(path, "set needs value or add"), -1;
            break;
        }
        case Kind::Action: {
            const Json* a = j.get("action");
            g.nodes[id].action = a && a->isString() ? a->asString() : name;
            break;
        }
        case Kind::Utility: {
            const Json* opts = j.get("options");
            if (!opts || !opts->isArray() || opts->size() == 0) return fail(path, "utility needs options"), -1;
            g.nodes[id].weighted = j.get("select") && j.get("select")->asString() == "weighted";
            if (j.get("select") && j.get("select")->asString() != "weighted" && j.get("select")->asString() != "best") return fail(path, "select must be best or weighted"), -1;
            g.nodes[id].power = static_cast<float>(j.get("power") ? j.get("power")->asNumber() : 1.0);
            g.nodes[id].stickiness = static_cast<float>(j.get("stickiness") ? j.get("stickiness")->asNumber() : 0.0);
            g.nodes[id].minScore = static_cast<float>(j.get("minScore") ? j.get("minScore")->asNumber() : 0.0);
            g.nodes[id].reconsider = !j.get("reconsider") || j.get("reconsider")->asBool();
            g.nodes[id].combine = j.get("combine") ? j.get("combine")->asString() : "product";
            if (g.nodes[id].combine != "product" && g.nodes[id].combine != "min" && g.nodes[id].combine != "max" && g.nodes[id].combine != "average" && g.nodes[id].combine != "sum")
                return fail(path, "bad combine"), -1;
            int i = 0;
            for (const Json& oj : opts->items()) {
                const std::string opath = path + "." + name + ".options[" + std::to_string(i++) + "]";
                Option option;
                const Json* on = oj.get("name");
                if (!on || !on->isString()) return fail(opath, "option needs name"), -1;
                option.name = on->asString();
                option.weight = static_cast<float>(oj.get("weight") ? oj.get("weight")->asNumber() : 1.0);
                if (const Json* cs = oj.get("considerations")) {
                    for (const Json& cj : cs->items()) {
                        option.considerations.emplace_back();
                        if (!consideration(cj, option.considerations.back(), opath)) return -1;
                    }
                }
                if (const Json* gate = oj.get("gate")) {
                    option.hasGate = true;
                    if (!cond(*gate, option.gate, opath + ".gate")) return -1;
                }
                const Json* nj = oj.get("node");
                if (!nj) return fail(opath, "option needs node"), -1;
                option.child = node(*nj, opath);
                if (option.child < 0) return -1;
                g.nodes[id].options.push_back(std::move(option));
            }
            break;
        }
        case Kind::Fsm: {
            const Json* states = j.get("states");
            if (!states || !states->isObject() || states->size() == 0) return fail(path, "fsm needs states"), -1;
            for (const auto& [sname, sj] : states->members()) {
                State st;
                st.name = sname;
                const std::string spath = path + "." + name + "." + sname;
                const Json* nj = sj.get("node");
                if (!nj) return fail(spath, "state needs node"), -1;
                st.child = node(*nj, spath);
                if (st.child < 0) return -1;
                if (const Json* tr = sj.get("transitions")) {
                    for (const Json& tj : tr->items()) {
                        st.transitions.emplace_back();
                        if (!transition(tj, st.transitions.back(), spath)) return -1;
                    }
                }
                g.nodes[id].states.push_back(std::move(st));
            }
            if (const Json* any = j.get("any")) {
                for (const Json& tj : any->items()) {
                    g.nodes[id].global.emplace_back();
                    if (!transition(tj, g.nodes[id].global.back(), path + "." + name + ".any")) return -1;
                }
            }
            g.nodes[id].initial = j.get("initial") ? j.get("initial")->asString() : g.nodes[id].states.front().name;
            const auto known = [&](const std::string& s) { return s == "$success" || s == "$failure" || std::any_of(g.nodes[id].states.begin(), g.nodes[id].states.end(), [&](const State& st) { return st.name == s; }); };
            if (!known(g.nodes[id].initial)) return fail(path, "unknown initial state"), -1;
            for (const State& st : g.nodes[id].states) {
                for (const Transition& t : st.transitions) {
                    if (!known(t.to)) return fail(path, "unknown transition target " + t.to), -1;
                }
            }
            for (const Transition& t : g.nodes[id].global) {
                if (!known(t.to)) return fail(path, "unknown transition target " + t.to), -1;
            }
            break;
        }
        }
        return id;
    }
};
}

BehaviorGraph::BehaviorGraph() : m_impl(std::make_unique<Impl>()) {}
BehaviorGraph::~BehaviorGraph() = default;
BehaviorGraph::BehaviorGraph(BehaviorGraph&&) noexcept = default;
BehaviorGraph& BehaviorGraph::operator=(BehaviorGraph&&) noexcept = default;

bool BehaviorGraph::load(const Json& doc, std::string* error) {
    auto next = std::make_unique<Impl>();
    Parser parser{*next, {}};
    const Json* root = doc.get("root");
    if (!root) {
        if (error) *error = "missing root";
        return false;
    }
    if (const Json* n = doc.get("name")) next->name = n->asString();
    if (const Json* bb = doc.get("blackboard")) next->initial = *bb;
    next->root = parser.node(*root, "root");
    if (next->root < 0) {
        if (error) *error = parser.error;
        return false;
    }
    m_impl = std::move(next);
    return true;
}

bool BehaviorGraph::loadText(std::string_view text, std::string* error) {
    const auto doc = Json::parse(text, error);
    return doc && load(*doc, error);
}

bool BehaviorGraph::loadFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return loadText(ss.str(), error);
}

const std::string& BehaviorGraph::name() const { return m_impl->name; }
size_t BehaviorGraph::nodeCount() const { return m_impl->nodes.size(); }
bool BehaviorGraph::valid() const { return m_impl->root >= 0; }

struct BehaviorRunner::Impl {
    std::vector<NodeState> states;
    std::vector<int> currentState;
    std::vector<int> option;
    std::vector<uint8_t> running;
};

namespace {
std::string condText(const Cond& c) {
    switch (c.type) {
    case Cond::T::Always: return "always";
    case Cond::T::Leaf: {
        if (c.op.empty()) return c.key;
        std::string v = !c.valueKey.empty() ? c.valueKey : (c.value.isString() ? c.value.asString() : fmt(toNumber(c.value)));
        return c.key + " " + c.op + " " + v;
    }
    case Cond::T::Not: return "not (" + condText(c.kids[0]) + ")";
    default: {
        std::string out;
        for (size_t i = 0; i < c.kids.size(); ++i) out += (i ? (c.type == Cond::T::All ? " and " : " or ") : "") + condText(c.kids[i]);
        return "(" + out + ")";
    }
    }
}

bool evalCond(const Cond& c, const Blackboard& bb) {
    switch (c.type) {
    case Cond::T::Always: return true;
    case Cond::T::Not: return !evalCond(c.kids[0], bb);
    case Cond::T::All:
        for (const Cond& k : c.kids) {
            if (!evalCond(k, bb)) return false;
        }
        return true;
    case Cond::T::Any:
        for (const Cond& k : c.kids) {
            if (evalCond(k, bb)) return true;
        }
        return false;
    case Cond::T::Leaf: {
        const Json* lhs = bb.get(c.key);
        if (c.op.empty()) return lhs && (lhs->isBool() ? lhs->asBool() : (lhs->isNumber() ? lhs->asNumber() != 0.0 : !lhs->asString().empty()));
        if (!lhs) return c.op == "!=";
        Json rhs = c.value;
        if (!c.valueKey.empty()) {
            const Json* r = bb.get(c.valueKey);
            if (!r) return c.op == "!=";
            rhs = *r;
        }
        if (lhs->isString() || rhs.isString()) {
            const bool eq = lhs->isString() && rhs.isString() && lhs->asString() == rhs.asString();
            return c.op == "==" ? eq : (c.op == "!=" ? !eq : false);
        }
        const double a = toNumber(*lhs);
        const double b = toNumber(rhs);
        if (c.op == "<") return a < b;
        if (c.op == "<=") return a <= b;
        if (c.op == ">") return a > b;
        if (c.op == ">=") return a >= b;
        if (c.op == "==") return a == b;
        return a != b;
    }
    }
    return false;
}
}

BehaviorRunner::BehaviorRunner(const BehaviorGraph& graph, const ActionRegistry& actions, Pcg32& rng, uint32_t agent)
    : m_impl(std::make_unique<Impl>()), m_graph(graph), m_actions(actions), m_rng(rng), m_agent(agent) {
    reset();
}

BehaviorRunner::~BehaviorRunner() = default;

void BehaviorRunner::reset() {
    const size_t n = m_graph.impl().nodes.size();
    m_impl->states.assign(n, NodeState{});
    m_impl->currentState.assign(n, -1);
    m_impl->option.assign(n, -1);
    m_impl->running.assign(n, 0);
    m_tick = 0;
    m_trace.clear();
    m_blackboard.clear();
    for (const auto& [k, v] : m_graph.impl().initial.members()) m_blackboard.set(k, v);
}

namespace {
struct Exec {
    const BehaviorGraph::Impl& g;
    BehaviorRunner::Impl& rt;
    const ActionRegistry& actions;
    Blackboard& bb;
    DecisionTrace& trace;
    Pcg32& rng;
    float dt;
    uint64_t tick;
    uint32_t agent;

    void resetSubtree(int id) {
        rt.states[id] = NodeState{};
        rt.currentState[id] = -1;
        rt.option[id] = -1;
        rt.running[id] = 0;
        const Node& n = g.nodes[id];
        for (const int c : n.children) resetSubtree(c);
        for (const Option& o : n.options) resetSubtree(o.child);
        for (const State& s : n.states) resetSubtree(s.child);
    }

    void emit(const Node& n, const char* kind, std::string winner, std::string reason, std::vector<TraceOption> options = {}) {
        TraceEvent e;
        e.tick = tick;
        e.agent = agent;
        e.node = n.name;
        e.kind = kind;
        e.winner = std::move(winner);
        e.reason = std::move(reason);
        e.options = std::move(options);
        trace.record(std::move(e));
    }

    Status run(int id) {
        const Status s = exec(id);
        rt.running[id] = s == Status::Running ? 1 : 0;
        if (s != Status::Running) resetSubtree(id);
        return s;
    }

    Status exec(int id) {
        const Node& n = g.nodes[id];
        NodeState& st = rt.states[id];
        switch (n.kind) {
        case Kind::Sequence:
        case Kind::Selector: {
            const bool seq = n.kind == Kind::Sequence;
            if (n.reactive) {
                const int prev = st.last;
                st.index = 0;
                (void)prev;
            }
            for (int i = st.index; i < static_cast<int>(n.children.size()); ++i) {
                if (n.reactive && st.last >= 0 && st.last != i && rt.running[n.children[st.last]]) {
                    resetSubtree(n.children[st.last]);
                    st.last = -1;
                }
                const Status s = run(n.children[i]);
                if (s == Status::Running) {
                    st.index = n.reactive ? 0 : i;
                    st.last = i;
                    return Status::Running;
                }
                if (seq ? s == Status::Failure : s == Status::Success) return s;
            }
            return seq ? Status::Success : Status::Failure;
        }
        case Kind::Parallel: {
            int ok = 0;
            int bad = 0;
            for (const int c : n.children) {
                const Status s = run(c);
                if (s == Status::Success) ++ok;
                if (s == Status::Failure) ++bad;
            }
            const int total = static_cast<int>(n.children.size());
            if (n.parallelAny) {
                if (ok > 0) return Status::Success;
                return bad == total ? Status::Failure : Status::Running;
            }
            if (bad > 0) return Status::Failure;
            return ok == total ? Status::Success : Status::Running;
        }
        case Kind::Inverter: {
            const Status s = run(n.children[0]);
            return s == Status::Running ? s : (s == Status::Success ? Status::Failure : Status::Success);
        }
        case Kind::Succeeder: {
            const Status s = run(n.children[0]);
            return s == Status::Running ? s : Status::Success;
        }
        case Kind::Failer: {
            const Status s = run(n.children[0]);
            return s == Status::Running ? s : Status::Failure;
        }
        case Kind::Repeat: {
            const Status s = run(n.children[0]);
            if (s == Status::Running) return s;
            if (s == Status::Failure) return Status::Failure;
            ++st.counter;
            if (n.count >= 0 && st.counter >= n.count) return Status::Success;
            return Status::Running;
        }
        case Kind::Wait:
            st.timer += dt;
            return st.timer >= n.seconds ? Status::Success : Status::Running;
        case Kind::Condition: return evalCond(n.cond, bb) ? Status::Success : Status::Failure;
        case Kind::Set: {
            if (n.add) bb.setNumber(n.key, bb.number(n.key) + toNumber(n.value));
            else if (!n.valueKey.empty()) {
                if (const Json* v = bb.get(n.valueKey)) bb.set(n.key, *v);
            } else bb.set(n.key, n.value);
            return Status::Success;
        }
        case Kind::Action: {
            const ActionFn* fn = actions.find(n.action);
            if (!fn) {
                emit(n, "action", n.action, "unknown action");
                return Status::Failure;
            }
            AiContext ctx{bb, rng, dt, tick, agent};
            const Status s = (*fn)(ctx);
            emit(n, "action", n.action, s == Status::Running ? "running" : (s == Status::Success ? "success" : "failure"));
            return s;
        }
        case Kind::Utility: return utility(id);
        case Kind::Fsm: return fsm(id);
        }
        return Status::Failure;
    }

    bool transitionFires(const Transition& t, const NodeState& st) {
        if (t.after >= 0.0f && st.stateTime < t.after) return false;
        return evalCond(t.when, bb);
    }

    std::string transitionText(const Transition& t) {
        std::string s;
        if (t.after >= 0.0f) s = "after " + fmt(t.after) + "s";
        if (t.when.type != Cond::T::Always) s += (s.empty() ? "" : " and ") + condText(t.when);
        return s;
    }

    Status fsm(int id) {
        const Node& n = g.nodes[id];
        NodeState& st = rt.states[id];
        int& cur = rt.currentState[id];
        const auto indexOf = [&](const std::string& name) {
            for (size_t i = 0; i < n.states.size(); ++i) {
                if (n.states[i].name == name) return static_cast<int>(i);
            }
            return -1;
        };
        if (cur < 0) {
            if (n.initial == "$success") return Status::Success;
            if (n.initial == "$failure") return Status::Failure;
            cur = indexOf(n.initial);
            st.stateTime = 0.0f;
            emit(n, "fsm", n.initial, "initial state");
        }
        st.stateTime += dt;
        const Transition* fired = nullptr;
        for (const Transition& t : n.global) {
            if (n.states[cur].name != t.to && transitionFires(t, st)) {
                fired = &t;
                break;
            }
        }
        if (!fired) {
            for (const Transition& t : n.states[cur].transitions) {
                if (transitionFires(t, st)) {
                    fired = &t;
                    break;
                }
            }
        }
        if (fired) {
            emit(n, "fsm", fired->to, "from " + n.states[cur].name + " when " + transitionText(*fired));
            resetSubtree(n.states[cur].child);
            if (fired->to == "$success") return Status::Success;
            if (fired->to == "$failure") return Status::Failure;
            cur = indexOf(fired->to);
            st.stateTime = 0.0f;
        }
        run(n.states[cur].child);
        return Status::Running;
    }

    double score(const Node& n, const Option& o, TraceOption& rec) {
        rec.name = o.name;
        if (o.hasGate && !evalCond(o.gate, bb)) {
            rec.available = false;
            return -1.0;
        }
        double result = n.combine == "min" ? 1.0 : (n.combine == "product" ? 1.0 : 0.0);
        if (n.combine == "min") result = 1.0e30;
        double sum = 0.0;
        for (const Consideration& c : o.considerations) {
            const double v = bb.number(c.input);
            const float x = std::clamp(static_cast<float>((v - c.minimum) / (c.maximum - c.minimum)), 0.0f, 1.0f);
            const double s = std::clamp(c.curve.eval(x), 0.0f, 1.0f);
            rec.considerations.push_back({c.input, v, s});
            sum += s;
            if (n.combine == "product") result *= s;
            else if (n.combine == "min") result = std::min(result, s);
            else if (n.combine == "max") result = std::max(result, s);
        }
        const double count = static_cast<double>(o.considerations.size());
        if (count == 0) result = 1.0;
        else if (n.combine == "product" && count > 1) result += (1.0 - result) * (1.0 - 1.0 / count) * result;
        else if (n.combine == "average") result = sum / count;
        else if (n.combine == "sum") result = sum;
        return result * o.weight;
    }

    Status utility(int id) {
        const Node& n = g.nodes[id];
        int& cur = rt.option[id];
        if (cur >= 0 && !n.reconsider && rt.running[n.options[cur].child]) {
            emit(n, "utility", n.options[cur].name, "kept running (no reconsider)");
            return run(n.options[cur].child);
        }
        std::vector<TraceOption> recs(n.options.size());
        std::vector<double> scores(n.options.size());
        for (size_t i = 0; i < n.options.size(); ++i) {
            scores[i] = score(n, n.options[i], recs[i]);
            if (static_cast<int>(i) == cur && recs[i].available) scores[i] += n.stickiness;
            recs[i].score = scores[i];
        }
        int best = -1;
        for (size_t i = 0; i < scores.size(); ++i) {
            if (recs[i].available && (best < 0 || scores[i] > scores[best])) best = static_cast<int>(i);
        }
        std::string reason;
        int pick = best;
        if (best < 0 || scores[best] < n.minScore) {
            emit(n, "utility", "", best < 0 ? "no available option" : "best score " + fmt(scores[best]) + " below minScore " + fmt(n.minScore), std::move(recs));
            if (cur >= 0) resetSubtree(n.options[cur].child);
            cur = -1;
            return Status::Failure;
        }
        if (n.weighted) {
            double total = 0.0;
            std::vector<double> w(scores.size(), 0.0);
            for (size_t i = 0; i < scores.size(); ++i) {
                if (recs[i].available && scores[i] >= n.minScore && scores[i] > 0.0) {
                    w[i] = std::pow(scores[i], n.power);
                    total += w[i];
                }
            }
            if (total > 0.0) {
                const double r = rng.uniform() * total;
                double acc = 0.0;
                for (size_t i = 0; i < w.size(); ++i) {
                    if (w[i] <= 0.0) continue;
                    acc += w[i];
                    pick = static_cast<int>(i);
                    if (r < acc) break;
                }
                reason = "weighted pick " + fmt(w[pick] / total) + " of probability mass";
            } else reason = "highest score " + fmt(scores[best]);
        } else {
            int second = -1;
            for (size_t i = 0; i < scores.size(); ++i) {
                if (static_cast<int>(i) != best && recs[i].available && (second < 0 || scores[i] > scores[second])) second = static_cast<int>(i);
            }
            reason = "highest score " + fmt(scores[best]);
            if (second >= 0) reason += " over '" + n.options[second].name + "' " + fmt(scores[second]);
            if (best == cur && n.stickiness > 0.0f) reason += " (stickiness " + fmt(n.stickiness) + ")";
        }
        emit(n, "utility", n.options[pick].name, reason, std::move(recs));
        if (cur >= 0 && cur != pick) resetSubtree(n.options[cur].child);
        cur = pick;
        const Status s = run(n.options[pick].child);
        if (s != Status::Running) cur = -1;
        return s;
    }
};
}

Status BehaviorRunner::tick(float dt) {
    if (!m_graph.valid()) return Status::Failure;
    Exec ex{m_graph.impl(), *m_impl, m_actions, m_blackboard, m_trace, m_rng, dt, m_tick, m_agent};
    const Status s = ex.run(m_graph.impl().root);
    ++m_tick;
    return s;
}

std::vector<std::string> BehaviorRunner::activePath() const {
    std::vector<std::string> out;
    const auto& nodes = m_graph.impl().nodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (m_impl->running[i]) out.push_back(nodes[i].name);
    }
    return out;
}

std::string BehaviorRunner::stateOf(const std::string& fsmName) const {
    const auto& nodes = m_graph.impl().nodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].kind == Kind::Fsm && nodes[i].name == fsmName) {
            const int s = m_impl->currentState[i];
            return s < 0 ? std::string() : nodes[i].states[s].name;
        }
    }
    return {};
}

uint64_t BehaviorRunner::stateHash() const {
    uint64_t h = hashName(m_blackboard.toJson().dump());
    for (size_t i = 0; i < m_impl->states.size(); ++i) {
        const NodeState& s = m_impl->states[i];
        h ^= static_cast<uint64_t>(s.index + 1) * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(m_impl->currentState[i] + 2) * 31u + static_cast<uint64_t>(m_impl->option[i] + 2) * 131u + static_cast<uint64_t>(s.counter) * 7u;
        h = (h << 13) | (h >> 51);
    }
    return h;
}
