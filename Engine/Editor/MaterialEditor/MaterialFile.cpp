#include "MaterialFile.hpp"
#include <nlohmann/json.hpp>
#include "LivingMaterial.hpp"
#include "MaterialLayer.hpp"
#include "DynamicRule.hpp"
#include "Mask.hpp"
#include "Editor.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

static int ToInt(DynamicRuleOperator op) { return static_cast<int>(op); }
static int ToInt(DynamicRuleLeftOperand op) { return static_cast<int>(op); }
static int ToInt(DynamicRuleRightOperand op) { return static_cast<int>(op); }

void MaterialFile::Save(const fs::path& materialPath, const LivingMaterial& material) {
    json jsonData;

    jsonData["name"] = material.m_Name;
    jsonData["layers"] = json::array();

    for (const auto& layer : material.m_Layers) {
        json layerData;
        layerData["name"] = layer.m_Name;
        layerData["isEnabled"] = layer.m_IsEnabled;
        layerData["baseMaterialID"] = layer.m_BaseMaterialID;

        if (!layer.m_ParameterOverrides.empty()) {
            json paramOverrides = json::object();
            for (const auto& [key, vec] : layer.m_ParameterOverrides) {
                paramOverrides[key] = { vec.x, vec.y, vec.z, vec.w };
            }
            layerData["parameterOverrides"] = std::move(paramOverrides);
        } else {
            layerData["parameterOverrides"] = json::object();
        }

        {
            json maskObj = json::object();
            maskObj["texture"] = layer.m_Mask.pathToMaskTexture.empty()
                ? std::string("")
                : layer.m_Mask.pathToMaskTexture.string();
            layerData["mask"] = std::move(maskObj);
        }

        if (!layer.m_Rules.empty()) {
            json rulesArr = json::array();
            for (const auto& rule : layer.m_Rules) {
                json ruleData;
                ruleData["name"] = rule.name;
                ruleData["op"] = ToInt(rule.op);
                ruleData["leftOperand"] = ToInt(rule.leftOperand);
                ruleData["rightOperand"] = ToInt(rule.rightOperand);

                switch (rule.rightOperand) {
                    case DynamicRuleRightOperand::FloatingPoint:
                        ruleData["valueFloat"] = rule.rightValueFloat;
                        break;
                    case DynamicRuleRightOperand::Integer:
                        ruleData["valueInt"] = rule.rightValueInt;
                        break;
                    case DynamicRuleRightOperand::Boolean:
                        ruleData["valueBool"] = rule.rightValueBool;
                        break;
                    case DynamicRuleRightOperand::String:
                        ruleData["valueString"] = rule.rightValueString;
                        break;
                    case DynamicRuleRightOperand::Entity:
                        ruleData["entityId"] = rule.rightValueEntity;
                        break;
                }
                rulesArr.push_back(std::move(ruleData));
            }
            layerData["rules"] = std::move(rulesArr);
        } else {
            layerData["rules"] = json::array();
        }

        jsonData["layers"].push_back(std::move(layerData));
    }

    std::ofstream file(materialPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open material file for saving: " + materialPath.string());
    }
    file << jsonData.dump(4);
    file.close();
}

static DynamicRuleOperator ParseOp(int v) {
    switch (v) {
        case 0: return DynamicRuleOperator::Equal;
        case 1: return DynamicRuleOperator::NotEqual;
        case 2: return DynamicRuleOperator::GreaterThan;
        case 3: return DynamicRuleOperator::LessThan;
        case 4: return DynamicRuleOperator::GreaterThanOrEqual;
        case 5: return DynamicRuleOperator::LessThanOrEqual;
        default: return DynamicRuleOperator::GreaterThan;
    }
}
static DynamicRuleLeftOperand ParseLeft(int v) {
    switch (v) {
        case 0: return DynamicRuleLeftOperand::Humidity;
        case 1: return DynamicRuleLeftOperand::Temperature;
        case 2: return DynamicRuleLeftOperand::WindSpeed;
        case 3: return DynamicRuleLeftOperand::RainIntensity;
        case 4: return DynamicRuleLeftOperand::Age;
        case 5: return DynamicRuleLeftOperand::Slope;
        case 6: return DynamicRuleLeftOperand::Exposure;
        case 7: return DynamicRuleLeftOperand::Wetness;
        case 8: return DynamicRuleLeftOperand::ProximityToObject;
        default: return DynamicRuleLeftOperand::Humidity;
    }
}
static DynamicRuleRightOperand ParseRight(int v) {
    switch (v) {
        case 0: return DynamicRuleRightOperand::FloatingPoint;
        case 1: return DynamicRuleRightOperand::Integer;
        case 2: return DynamicRuleRightOperand::Boolean;
        case 3: return DynamicRuleRightOperand::String;
        case 4: return DynamicRuleRightOperand::Entity;
        default: return DynamicRuleRightOperand::FloatingPoint;
    }
}

void MaterialFile::Load(const fs::path& materialPath) {
    std::ifstream file(materialPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open material file for loading: " + materialPath.string());
    }

    nlohmann::json jsonData;
    if (file.peek() == std::ifstream::traits_type::eof()) {
        jsonData = nlohmann::json::object();
    } else {
        file >> jsonData;
    }
    file.close();

    LivingMaterial loaded;
    loaded.m_Name = jsonData.value("name", std::string("UnnamedMaterial"));

    loaded.m_Layers.clear();
    if (jsonData.contains("layers") && jsonData["layers"].is_array()) {
        for (const auto& layerData : jsonData["layers"]) {
            MaterialLayer layer;
            layer.m_Name = layerData.value("name", std::string("Layer"));
            layer.m_IsEnabled = layerData.value("isEnabled", true);
            layer.m_BaseMaterialID = layerData.value("baseMaterialID", std::string(""));

            if (layerData.contains("parameterOverrides") && layerData["parameterOverrides"].is_object()) {
                for (const auto& param : layerData["parameterOverrides"].items()) {
                    const auto& v = param.value();
                    if (v.is_array() && v.size() == 4) {
                        layer.m_ParameterOverrides[param.key()] = {
                            v[0].get<float>(),
                            v[1].get<float>(),
                            v[2].get<float>(),
                            v[3].get<float>()
                        };
                    }
                }
            }

            if (layerData.contains("mask") && layerData["mask"].is_object()) {
                const auto& maskObj = layerData["mask"];
                if (maskObj.contains("texture") && !maskObj["texture"].is_null()) {
                    std::string maskPath = maskObj["texture"].get<std::string>();
                    if (!maskPath.empty()) {
                        layer.m_Mask.pathToMaskTexture = fs::path(maskPath);
                    }
                }
            }

            if (layerData.contains("rules") && layerData["rules"].is_array()) {
                for (const auto& ruleData : layerData["rules"]) {
                    DynamicRule rule;
                    if (ruleData.contains("name")) rule.name = ruleData["name"].get<std::string>();
                    rule.op = ParseOp(ruleData.value("op", 2));
                    rule.leftOperand = ParseLeft(ruleData.value("leftOperand", 0));
                    rule.rightOperand = ParseRight(ruleData.value("rightOperand", 0));

                    switch (rule.rightOperand) {
                        case DynamicRuleRightOperand::FloatingPoint:
                            rule.rightValueFloat = ruleData.value("valueFloat", 0.0f);
                            break;
                        case DynamicRuleRightOperand::Integer:
                            rule.rightValueInt = ruleData.value("valueInt", 0);
                            break;
                        case DynamicRuleRightOperand::Boolean:
                            rule.rightValueBool = ruleData.value("valueBool", false);
                            break;
                        case DynamicRuleRightOperand::String:
                            rule.rightValueString = ruleData.value("valueString", std::string());
                            break;
                        case DynamicRuleRightOperand::Entity:
                            rule.rightValueEntity = ruleData.value("entityId", static_cast<std::uint64_t>(0));
                            break;
                    }

                    layer.m_Rules.push_back(std::move(rule));
                }
            }

            loaded.m_Layers.push_back(std::move(layer));
        }
    }

    materialEditor.m_SelectedMaterialPath = materialPath;
    materialEditor.m_ActiveMaterial = std::move(loaded);
}