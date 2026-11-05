module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;
import Engine.Json;

#include "AIInternal.hpp"

namespace AiDetail {

namespace {
constexpr float kDefaultMaximum = 1.0f;
constexpr int kMaxRepeatCount = 1 << 30;
constexpr const char* kSuccessTarget = "$success";
constexpr const char* kFailureTarget = "$failure";

const std::map<std::string, Kind>& nodeKinds() {
    static const std::map<std::string, Kind> kinds = {{"sequence", Kind::Sequence},   {"selector", Kind::Selector}, {"parallel", Kind::Parallel},   {"inverter", Kind::Inverter},
                                                      {"succeeder", Kind::Succeeder}, {"failer", Kind::Failer},     {"repeat", Kind::Repeat},       {"wait", Kind::Wait},
                                                      {"condition", Kind::Condition}, {"set", Kind::Set},           {"action", Kind::Action},       {"utility", Kind::Utility},
                                                      {"fsm", Kind::Fsm}};
    return kinds;
}

bool parseOperator(const std::string& text, CompareOp& out) {
    static const std::map<std::string, CompareOp> operators = {{"<", CompareOp::Less},       {"<=", CompareOp::LessEqual}, {">", CompareOp::Greater},
                                                               {">=", CompareOp::GreaterEqual}, {"==", CompareOp::Equal},     {"!=", CompareOp::NotEqual}};
    const auto found = operators.find(text);
    if (found == operators.end()) return false;
    out = found->second;
    return true;
}

bool parseCombine(const std::string& text, CombineMode& out) {
    static const std::map<std::string, CombineMode> modes = {{"product", CombineMode::Product}, {"min", CombineMode::Min}, {"max", CombineMode::Max},
                                                             {"average", CombineMode::Average}, {"sum", CombineMode::Sum}};
    const auto found = modes.find(text);
    if (found == modes.end()) return false;
    out = found->second;
    return true;
}

double numberOr(const Json& json, const char* field, double fallback) {
    const Json* value = json.get(field);
    return value ? value->asNumber() : fallback;
}

struct Parser {
    BehaviorGraph::Impl& graph;
    std::string error;

    Node& nodeAt(int id) { return graph.nodes[static_cast<size_t>(id)]; }

    bool fail(const std::string& path, const std::string& what) {
        if (error.empty()) error = path + ": " + what;
        return false;
    }

    int failNode(const std::string& path, const std::string& what) {
        fail(path, what);
        return -1;
    }

    bool conditionList(const Json& list, Condition::Type type, Condition& out, const std::string& path) {
        out.type = type;
        if (!list.isArray()) return fail(path, "condition list must be array");
        for (const Json& item : list.items()) {
            out.kids.emplace_back();
            if (!condition(item, out.kids.back(), path)) return false;
        }
        return true;
    }

    bool condition(const Json& json, Condition& out, const std::string& path) {
        if (!json.isObject()) return fail(path, "condition must be object");
        if (const Json* all = json.get("all")) return conditionList(*all, Condition::Type::All, out, path + ".all");
        if (const Json* any = json.get("any")) return conditionList(*any, Condition::Type::Any, out, path + ".any");
        if (const Json* negated = json.get("not")) {
            out.type = Condition::Type::Not;
            out.kids.emplace_back();
            return condition(*negated, out.kids.back(), path + ".not");
        }
        const Json* key = json.get("key");
        if (!key || !key->isString()) return fail(path, "condition needs key");
        out.type = Condition::Type::Leaf;
        out.key = key->asString();
        if (const Json* op = json.get("op")) {
            if (!parseOperator(op->asString(), out.op)) return fail(path, "bad op " + op->asString());
        }
        if (const Json* value = json.get("value")) assignOperand(*value, out.value, out.valueKey);
        return true;
    }

    static void assignOperand(const Json& operand, Json& value, std::string& valueKey) {
        if (operand.isObject() && operand.get("key")) valueKey = operand.get("key")->asString();
        else value = operand;
    }

    bool curvePoints(const Json& curve, Consideration& out, const std::string& path) {
        out.curve.points.clear();
        for (const Json& point : curve.items()) {
            const Json* x = point.at(0);
            const Json* y = point.at(1);
            if (!x || !y) return fail(path, "curve point needs [x,y]");
            const float pointX = static_cast<float>(x->asNumber());
            if (!out.curve.points.empty() && pointX <= out.curve.points.back().first) return fail(path, "curve x must increase");
            out.curve.points.emplace_back(pointX, static_cast<float>(y->asNumber()));
        }
        if (out.curve.points.empty()) return fail(path, "empty curve");
        return true;
    }

    bool consideration(const Json& json, Consideration& out, const std::string& path) {
        const Json* input = json.get("input");
        if (!input || !input->isString()) return fail(path, "consideration needs input");
        out.input = input->asString();
        out.minimum = static_cast<float>(numberOr(json, "min", 0.0));
        out.maximum = static_cast<float>(numberOr(json, "max", kDefaultMaximum));
        if (out.maximum <= out.minimum) return fail(path, "max must exceed min");
        out.curve.points = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        if (const Json* curve = json.get("curve")) {
            if (!curvePoints(*curve, out, path)) return false;
        }
        if (const Json* mode = json.get("mode")) {
            if (mode->asString() == "smooth") out.curve.mode = CurveMode::Smooth;
            else if (mode->asString() == "step") out.curve.mode = CurveMode::Step;
            else if (mode->asString() != "linear") return fail(path, "bad curve mode");
        }
        return true;
    }

    bool transition(const Json& json, Transition& out, const std::string& path) {
        const Json* target = json.get("to");
        if (!target || !target->isString()) return fail(path, "transition needs to");
        out.to = target->asString();
        const Json* when = json.get("when");
        if (when && !condition(*when, out.when, path + ".when")) return false;
        const Json* after = json.get("after");
        if (after) out.after = static_cast<float>(after->asNumber());
        if (!when && !after) return fail(path, "transition needs when or after");
        return true;
    }

    bool childList(const Json& json, const char* field, int id, const std::string& path, std::vector<int>& out) {
        const Json* array = json.get(field);
        if (!array || !array->isArray()) return fail(path, std::string("needs ") + field + " array");
        int position = 0;
        for (const Json& child : array->items()) {
            const int childId = node(child, path + "." + nodeAt(id).name + "[" + std::to_string(position++) + "]");
            if (childId < 0) return false;
            out.push_back(childId);
        }
        return true;
    }

    bool singleChild(const Json& json, int id, const std::string& path) {
        const Json* child = json.get("child");
        if (!child) return fail(path, "needs child");
        const int childId = node(*child, path + "." + nodeAt(id).name);
        if (childId < 0) return false;
        nodeAt(id).children.push_back(childId);
        return true;
    }

    bool parseComposite(const Json& json, int id, const std::string& path) {
        std::vector<int> kids;
        if (!childList(json, "children", id, path, kids)) return false;
        Node& target = nodeAt(id);
        target.children = std::move(kids);
        if (target.kind == Kind::Parallel) target.parallelAny = json.get("policy") && json.get("policy")->asString() == "any";
        else target.reactive = json.get("reactive") && json.get("reactive")->asBool();
        return true;
    }

    bool parseRepeatCount(const Json& json, int id, const std::string& path) {
        const Json* count = json.get("count");
        if (!count) return true;
        if (!count->isNumber()) return fail(path, "repeat count must be number");
        nodeAt(id).count = static_cast<int>(std::clamp(count->asNumber(), -1.0, static_cast<double>(kMaxRepeatCount)));
        return true;
    }

    bool parseSet(const Json& json, int id, const std::string& path) {
        const Json* key = json.get("key");
        if (!key || !key->isString()) return fail(path, "set needs key");
        Node& target = nodeAt(id);
        target.key = key->asString();
        if (const Json* add = json.get("add")) {
            target.add = true;
            target.value = *add;
            return true;
        }
        const Json* value = json.get("value");
        if (!value) return fail(path, "set needs value or add");
        assignOperand(*value, target.value, target.valueKey);
        return true;
    }

    bool parseOption(const Json& optionJson, int id, const std::string& optionPath) {
        Option option;
        const Json* name = optionJson.get("name");
        if (!name || !name->isString()) return fail(optionPath, "option needs name");
        option.name = name->asString();
        option.weight = static_cast<float>(numberOr(optionJson, "weight", 1.0));
        if (const Json* considerations = optionJson.get("considerations")) {
            for (const Json& considerationJson : considerations->items()) {
                option.considerations.emplace_back();
                if (!consideration(considerationJson, option.considerations.back(), optionPath)) return false;
            }
        }
        if (const Json* gate = optionJson.get("gate")) {
            option.hasGate = true;
            if (!condition(*gate, option.gate, optionPath + ".gate")) return false;
        }
        const Json* childJson = optionJson.get("node");
        if (!childJson) return fail(optionPath, "option needs node");
        option.child = node(*childJson, optionPath);
        if (option.child < 0) return false;
        nodeAt(id).options.push_back(std::move(option));
        return true;
    }

    bool parseUtility(const Json& json, int id, const std::string& path) {
        const Json* options = json.get("options");
        if (!options || !options->isArray() || options->size() == 0) return fail(path, "utility needs options");
        const Json* select = json.get("select");
        if (select && select->asString() != "weighted" && select->asString() != "best") return fail(path, "select must be best or weighted");
        Node& target = nodeAt(id);
        target.weighted = select && select->asString() == "weighted";
        target.power = static_cast<float>(numberOr(json, "power", 1.0));
        target.stickiness = static_cast<float>(numberOr(json, "stickiness", 0.0));
        target.minScore = static_cast<float>(numberOr(json, "minScore", 0.0));
        target.reconsider = !json.get("reconsider") || json.get("reconsider")->asBool();
        const std::string combine = json.get("combine") ? json.get("combine")->asString() : "product";
        if (!parseCombine(combine, target.combine)) return fail(path, "bad combine");
        int position = 0;
        for (const Json& optionJson : options->items()) {
            const std::string optionPath = path + "." + nodeAt(id).name + ".options[" + std::to_string(position++) + "]";
            if (!parseOption(optionJson, id, optionPath)) return false;
        }
        return true;
    }

    bool stateTargetKnown(const Node& fsmNode, const std::string& name) const {
        if (name == kSuccessTarget || name == kFailureTarget) return true;
        return std::any_of(fsmNode.states.begin(), fsmNode.states.end(), [&](const State& state) { return state.name == name; });
    }

    bool validateFsmTargets(int id, const std::string& path) {
        const Node& fsmNode = nodeAt(id);
        if (!stateTargetKnown(fsmNode, fsmNode.initial)) return fail(path, "unknown initial state");
        for (const State& state : fsmNode.states) {
            for (const Transition& transitionEntry : state.transitions) {
                if (!stateTargetKnown(fsmNode, transitionEntry.to)) return fail(path, "unknown transition target " + transitionEntry.to);
            }
        }
        for (const Transition& transitionEntry : fsmNode.global) {
            if (!stateTargetKnown(fsmNode, transitionEntry.to)) return fail(path, "unknown transition target " + transitionEntry.to);
        }
        return true;
    }

    bool parseState(const std::string& stateName, const Json& stateJson, int id, const std::string& path) {
        State state;
        state.name = stateName;
        const std::string statePath = path + "." + nodeAt(id).name + "." + stateName;
        const Json* childJson = stateJson.get("node");
        if (!childJson) return fail(statePath, "state needs node");
        state.child = node(*childJson, statePath);
        if (state.child < 0) return false;
        if (const Json* transitions = stateJson.get("transitions")) {
            for (const Json& transitionJson : transitions->items()) {
                state.transitions.emplace_back();
                if (!transition(transitionJson, state.transitions.back(), statePath)) return false;
            }
        }
        nodeAt(id).states.push_back(std::move(state));
        return true;
    }

    bool parseFsm(const Json& json, int id, const std::string& path) {
        const Json* states = json.get("states");
        if (!states || !states->isObject() || states->size() == 0) return fail(path, "fsm needs states");
        for (const auto& [stateName, stateJson] : states->members()) {
            if (!parseState(stateName, stateJson, id, path)) return false;
        }
        if (const Json* any = json.get("any")) {
            for (const Json& transitionJson : any->items()) {
                nodeAt(id).global.emplace_back();
                if (!transition(transitionJson, nodeAt(id).global.back(), path + "." + nodeAt(id).name + ".any")) return false;
            }
        }
        nodeAt(id).initial = json.get("initial") ? json.get("initial")->asString() : nodeAt(id).states.front().name;
        return validateFsmTargets(id, path);
    }

    bool parseKindFields(const Json& json, int id, const std::string& path) {
        Node& target = nodeAt(id);
        switch (target.kind) {
        case Kind::Sequence:
        case Kind::Selector:
        case Kind::Parallel: return parseComposite(json, id, path);
        case Kind::Inverter:
        case Kind::Succeeder:
        case Kind::Failer: return singleChild(json, id, path);
        case Kind::Repeat:
            if (!singleChild(json, id, path)) return false;
            return parseRepeatCount(json, id, path);
        case Kind::Wait:
            target.seconds = static_cast<float>(numberOr(json, "seconds", 0.0));
            return true;
        case Kind::Condition: {
            const Json* when = json.get("when");
            if (!when || !condition(*when, nodeAt(id).condition, path + "." + nodeAt(id).name)) return fail(path, "condition node needs when");
            return true;
        }
        case Kind::Set: return parseSet(json, id, path);
        case Kind::Action: {
            const Json* action = json.get("action");
            target.action = action && action->isString() ? action->asString() : target.name;
            return true;
        }
        case Kind::Utility: return parseUtility(json, id, path);
        case Kind::Fsm: return parseFsm(json, id, path);
        }
        return true;
    }

    int node(const Json& json, const std::string& path) {
        if (!json.isObject()) return failNode(path, "node must be object");
        const Json* type = json.get("type");
        if (!type || !type->isString()) return failNode(path, "node needs type");
        const int id = static_cast<int>(graph.nodes.size());
        graph.nodes.emplace_back();
        const std::string& typeName = type->asString();
        const auto kind = nodeKinds().find(typeName);
        if (kind == nodeKinds().end()) return failNode(path, "unknown node type " + typeName);
        nodeAt(id).kind = kind->second;
        nodeAt(id).name = json.get("name") ? json.get("name")->asString() : typeName + "#" + std::to_string(id);
        if (!parseKindFields(json, id, path)) return -1;
        nodeAt(id).subtreeEnd = static_cast<int>(graph.nodes.size());
        return id;
    }
};
}

}

BehaviorGraph::BehaviorGraph() : m_impl(std::make_unique<Impl>()) {}
BehaviorGraph::~BehaviorGraph() = default;
BehaviorGraph::BehaviorGraph(BehaviorGraph&&) noexcept = default;
BehaviorGraph& BehaviorGraph::operator=(BehaviorGraph&&) noexcept = default;

bool BehaviorGraph::load(const Json& doc, std::string* error) {
    auto next = std::make_unique<Impl>();
    AiDetail::Parser parser{*next, {}};
    const Json* root = doc.get("root");
    if (!root) {
        if (error) *error = "missing root";
        return false;
    }
    if (const Json* name = doc.get("name")) next->name = name->asString();
    if (const Json* blackboard = doc.get("blackboard")) next->initial = *blackboard;
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
    const std::optional<std::string> text = readTextFile(path);
    if (!text) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    return loadText(*text, error);
}

const std::string& BehaviorGraph::name() const { return m_impl->name; }
size_t BehaviorGraph::nodeCount() const { return m_impl->nodes.size(); }
bool BehaviorGraph::valid() const { return m_impl->root >= 0; }
