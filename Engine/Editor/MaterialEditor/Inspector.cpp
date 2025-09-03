/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "Inspector.hpp"
#include "MaterialLibrary.hpp"
#include "Editor.hpp"

static const char* kLeftOperandNames[] = {
    "Humidity", "Temperature", "Wind Speed", "Rain Intensity", "Age", "Slope", "Exposure", "Wetness", "Proximity To Object"
};
static const char* kOpNames[] = { "==", "!=", ">", "<", ">=", "<=" };

void InspectorPanel::Render(LivingMaterial& material, int selectedLayer) {
    ImGui::Begin("Inspector");

    if (selectedLayer >= 0 && selectedLayer < (int)material.m_Layers.size()) {
        MaterialLayer& layer = material.m_Layers[selectedLayer];

        {
            char nameBuffer[256];
            memset(nameBuffer, 0, sizeof(nameBuffer));
            strncpy(nameBuffer, layer.m_Name.c_str(), sizeof(nameBuffer) - 1);
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                layer.m_Name = nameBuffer;
            }
        }

        ImGui::Checkbox("Enabled", &layer.m_IsEnabled);

        {
            const auto& availableMaterials = MaterialLibrary::GetInstance().GetAvailableMaterialIDs();
            const char* preview = layer.m_BaseMaterialID.empty() ? "<Select base material>" : layer.m_BaseMaterialID.c_str();
            if (ImGui::BeginCombo("Base Material", preview)) {
                for (const auto& matId : availableMaterials) {
                    bool is_selected = (layer.m_BaseMaterialID == matId);
                    if (ImGui::Selectable(matId.c_str(), is_selected)) {
                        layer.m_BaseMaterialID = matId;
                        // Reset overrides to defaults
                        layer.m_ParameterOverrides.clear();
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (!layer.m_BaseMaterialID.empty()) {
            if (ImGui::TreeNode("Parameter Overrides")) {
                BaseMaterialDefinition baseDef = MaterialLibrary::GetInstance().getDefinition(layer.m_BaseMaterialID);
                for (auto const& [key, defaultVal] : baseDef.m_DefaultParameters) {
                    if (layer.m_ParameterOverrides.find(key) == layer.m_ParameterOverrides.end()) {
                        layer.m_ParameterOverrides[key] = defaultVal;
                    }
                    float* rgba = (float*)&layer.m_ParameterOverrides[key];
                    ImGui::ColorEdit4(key.c_str(), rgba);
                }
                ImGui::TreePop();
            }
        }

        if (ImGui::TreeNode("Mask")) {
            std::string pathStr = layer.m_Mask.pathToMaskTexture.string();
            ImGui::TextWrapped("Path: %s", pathStr.empty() ? "<none>" : pathStr.c_str());

            if (ImGui::Button("Assign Default Mask")) {
                fs::path matFolder = "Assets/Materials";
                std::string folderName = material.m_Name.empty() ? std::string("Unnamed") : material.m_Name;
                fs::path layerMask = matFolder / folderName / "masks" / ("layer" + std::to_string(selectedLayer) + "_mask.png");
                layer.m_Mask.pathToMaskTexture = layerMask;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Mask")) {
                layer.m_Mask.pathToMaskTexture.clear();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Rules")) {
            if (ImGui::Button("+ Add Rule")) {
                DynamicRule rule;
                rule.name = "Rule";
                rule.leftOperand = DynamicRuleLeftOperand::Humidity;
                rule.op = DynamicRuleOperator::GreaterThan;
                rule.rightOperand = DynamicRuleRightOperand::FloatingPoint;
                rule.rightValueFloat = 0.5f;
                layer.m_Rules.push_back(rule);
            }

            for (int i = 0; i < (int)layer.m_Rules.size(); ++i) {
                ImGui::PushID(i);
                DynamicRule& r = layer.m_Rules[i];

                // Left operand
                int leftIdx = static_cast<int>(r.leftOperand);
                ImGui::Combo("Left", &leftIdx, kLeftOperandNames, IM_ARRAYSIZE(kLeftOperandNames));
                r.leftOperand = static_cast<DynamicRuleLeftOperand>(leftIdx);

                // Operator
                int opIdx = static_cast<int>(r.op);
                ImGui::Combo("Op", &opIdx, kOpNames, IM_ARRAYSIZE(kOpNames));
                r.op = static_cast<DynamicRuleOperator>(opIdx);

                DynamicRuleRightOperand inferredType = DynamicRuleRightOperand::FloatingPoint;
                if (r.leftOperand == DynamicRuleLeftOperand::ProximityToObject) {
                    inferredType = DynamicRuleRightOperand::Entity;
                } else {
                    inferredType = DynamicRuleRightOperand::FloatingPoint;
                }
                r.rightOperand = inferredType;

                if (r.rightOperand == DynamicRuleRightOperand::FloatingPoint) {
                    ImGui::DragFloat("Value", &r.rightValueFloat, 0.01f);
                } else if (r.rightOperand == DynamicRuleRightOperand::Integer) {
                    ImGui::DragInt("Value", &r.rightValueInt, 1.0f);
                } else if (r.rightOperand == DynamicRuleRightOperand::Boolean) {
                    ImGui::Checkbox("Value", &r.rightValueBool);
                } else if (r.rightOperand == DynamicRuleRightOperand::String) {
                    char buf[256] = {0};
                    strncpy(buf, r.rightValueString.c_str(), sizeof(buf) - 1);
                    if (ImGui::InputText("Value", buf, sizeof(buf))) {
                        r.rightValueString = buf;
                    }
                } else if (r.rightOperand == DynamicRuleRightOperand::Entity) {
                    ImGui::InputScalar("EntityId", ImGuiDataType_U64, &r.rightValueEntity);
                    ImGui::TextUnformatted("This rule requires baking (scene query).");
                }

                if (ImGui::Button("Remove Rule")) {
                    layer.m_Rules.erase(layer.m_Rules.begin() + i);
                    ImGui::PopID();
                    --i;
                    continue;
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            ImGui::TreePop();
        }
    }

    ImGui::End();
}