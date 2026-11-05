module;

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

module Engine.AI;

#include "AIInternal.hpp"

namespace AiDetail {

namespace {
constexpr size_t kNumberBufferSize = 40;

const char* operatorText(CompareOp op) {
    switch (op) {
    case CompareOp::Less: return "<";
    case CompareOp::LessEqual: return "<=";
    case CompareOp::Greater: return ">";
    case CompareOp::GreaterEqual: return ">=";
    case CompareOp::Equal: return "==";
    case CompareOp::NotEqual: return "!=";
    case CompareOp::None: break;
    }
    return "";
}

bool jsonTruthy(const Json& json) {
    if (json.isBool()) return json.asBool();
    if (json.isNumber()) return json.asNumber() != 0.0;
    return !json.asString().empty();
}

bool compareStrings(const Json& lhs, const Json& rhs, CompareOp op) {
    const bool equal = lhs.isString() && rhs.isString() && lhs.asString() == rhs.asString();
    if (op == CompareOp::Equal) return equal;
    if (op == CompareOp::NotEqual) return !equal;
    return false;
}

bool compareNumbers(double lhs, double rhs, CompareOp op) {
    switch (op) {
    case CompareOp::Less: return lhs < rhs;
    case CompareOp::LessEqual: return lhs <= rhs;
    case CompareOp::Greater: return lhs > rhs;
    case CompareOp::GreaterEqual: return lhs >= rhs;
    case CompareOp::Equal: return lhs == rhs;
    default: return lhs != rhs;
    }
}

bool evaluateLeaf(const Condition& condition, const Blackboard& blackboard) {
    const Json* lhs = blackboard.get(condition.key);
    if (condition.op == CompareOp::None) return lhs && jsonTruthy(*lhs);
    if (!lhs) return condition.op == CompareOp::NotEqual;
    const Json* rhs = &condition.value;
    if (!condition.valueKey.empty()) {
        rhs = blackboard.get(condition.valueKey);
        if (!rhs) return condition.op == CompareOp::NotEqual;
    }
    if (lhs->isString() || rhs->isString()) return compareStrings(*lhs, *rhs, condition.op);
    return compareNumbers(toNumber(*lhs), toNumber(*rhs), condition.op);
}
}

double toNumber(const Json& json) {
    if (json.isBool()) return json.asBool() ? 1.0 : 0.0;
    return json.asNumber();
}

std::string formatNumber(double value) {
    char buffer[kNumberBufferSize];
    std::snprintf(buffer, sizeof buffer, "%.4g", value);
    return buffer;
}

std::string conditionText(const Condition& condition) {
    switch (condition.type) {
    case Condition::Type::Always: return "always";
    case Condition::Type::Leaf: {
        if (condition.op == CompareOp::None) return condition.key;
        const std::string operand = !condition.valueKey.empty() ? condition.valueKey : (condition.value.isString() ? condition.value.asString() : formatNumber(toNumber(condition.value)));
        return condition.key + " " + operatorText(condition.op) + " " + operand;
    }
    case Condition::Type::Not: return "not (" + conditionText(condition.kids[0]) + ")";
    default: {
        const char* separator = condition.type == Condition::Type::All ? " and " : " or ";
        std::string out;
        for (size_t index = 0; index < condition.kids.size(); ++index) {
            if (index > 0) out += separator;
            out += conditionText(condition.kids[index]);
        }
        return "(" + out + ")";
    }
    }
}

bool evaluateCondition(const Condition& condition, const Blackboard& blackboard) {
    switch (condition.type) {
    case Condition::Type::Always: return true;
    case Condition::Type::Not: return !evaluateCondition(condition.kids[0], blackboard);
    case Condition::Type::All:
        for (const Condition& kid : condition.kids) {
            if (!evaluateCondition(kid, blackboard)) return false;
        }
        return true;
    case Condition::Type::Any:
        for (const Condition& kid : condition.kids) {
            if (evaluateCondition(kid, blackboard)) return true;
        }
        return false;
    case Condition::Type::Leaf: return evaluateLeaf(condition, blackboard);
    }
    return false;
}

}
