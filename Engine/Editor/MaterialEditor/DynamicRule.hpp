/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_DYNAMIC_RULE_HPP
#define MATERIAL_DYNAMIC_RULE_HPP

#include <string>
#include <cstdint>

enum class DynamicRuleOperator {
    Equal = 0,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual
};

enum class DynamicRuleLeftOperand {
    Humidity = 0,
    Temperature,
    WindSpeed,
    RainIntensity,
    Age,
    Slope, // Maybe bake or use some kind of probes or depth math?
    Exposure,
    Wetness,
    ProximityToObject // Bake does not work for dynamic objects. maybe probes??
};

enum class DynamicRuleRightOperand {
    FloatingPoint = 0,
    Integer,
    Boolean,
    String,
    Entity
};

struct DynamicRule {
    std::string name;

    DynamicRuleOperator op = DynamicRuleOperator::GreaterThan;
    DynamicRuleLeftOperand leftOperand = DynamicRuleLeftOperand::Humidity;
    DynamicRuleRightOperand rightOperand = DynamicRuleRightOperand::FloatingPoint;

    float    rightValueFloat = 0.0f;
    int      rightValueInt = 0;
    bool     rightValueBool = false;
    std::string rightValueString;
    std::uint64_t rightValueEntity = 0;
};

inline bool RuleRequiresBake(DynamicRuleLeftOperand left) {
    switch (left) {
        case DynamicRuleLeftOperand::ProximityToObject:
        case DynamicRuleLeftOperand::Slope:
            return true;
        default:
            return false;
    }
}

#endif // MATERIAL_DYNAMIC_RULE_HPP