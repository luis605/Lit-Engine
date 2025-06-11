/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Core.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Core/SaveLoad.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Editor/Styles/ImGuiExtras.hpp>
#include <extras/IconsFontAwesome6.h>
#include <filesystem>

namespace fs = std::filesystem;

#define ICON_FA_COG_UTF8 "\xEF\x80\x93"

void EntityInspector() {
    if (selectedEntity == nullptr || !selectedEntity->getFlag(Entity::Flag::INITIALIZED)) {
        ImGui::Text("No entity selected.");
        return;
    }

    selectedEntityPosition = selectedEntity->getFlag(Entity::Flag::IS_CHILD)
                                 ? selectedEntity->relativePosition
                                 : selectedEntity->position;
    selectedEntityScale = selectedEntity->scale;

    // --- Entity Name Input ---
    char inputBuffer[255];
    strncpy(inputBuffer, selectedEntity->name.c_str(), sizeof(inputBuffer) - 1);
    inputBuffer[sizeof(inputBuffer) - 1] = '\0';

    ImGui::Text("Inspecting");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##EntityName", inputBuffer, sizeof(inputBuffer))) {
        selectedEntity->name = inputBuffer;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 8.0f));
    ImGui::Spacing();

    // --- Transform Section ---
    constexpr const char* transform_cstr = ICON_FA_SLIDERS " Transform";
    if (ImGui::CollapsingHeader(transform_cstr)) {
        ImGui::Indent(20.0f);
        if (ImGui::BeginTable("Transform", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            // Position
            ImGui::PushID("TRANSFORM-POSITION-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Position");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##Position", &selectedEntityPosition.x, 0.1f)) {
                if (selectedEntity->getFlag(Entity::Flag::IS_CHILD)) {
                    selectedEntity->relativePosition = selectedEntityPosition;
                } else {
                    if (selectedEntity->getFlag(Entity::Flag::IS_DYNAMIC) && selectedEntity->getFlag(Entity::Flag::CALC_PHYSICS)) {
                        selectedEntity->applyForce(selectedEntityPosition);
                    } else {
                        selectedEntity->position = selectedEntityPosition;
                    }
                }
            }
            ImGui::PopID();

            // Scale
            ImGui::PushID("TRANSFORM-SCALE-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Scale");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##Scale", &selectedEntity->scale.x, 0.1f)) {
                selectedEntity->reloadRigidBody();
            }
            ImGui::PopID();

            // Rotation X
            ImGui::PushID("TRANSFORM-ROTATION-X-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Rotation X");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (EntityRotationXInputModel) {
                if (ImGui::InputFloat("##RotationX", &selectedEntity->rotation.x, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                    EntityRotationXInputModel = false;
                    selectedEntity->reloadRigidBody();
                }
            } else {
                if (ImGui::SliderFloat("##RotationX", &selectedEntity->rotation.x, -180.0f, 180.0f, "%.3f")) {
                    selectedEntity->reloadRigidBody();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    EntityRotationXInputModel = true;
                }
            }
            ImGui::PopID();

            // Rotation Y
            ImGui::PushID("TRANSFORM-ROTATION-Y-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Rotation Y");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (EntityRotationYInputModel) {
                if (ImGui::InputFloat("##RotationY", &selectedEntity->rotation.y, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                    EntityRotationYInputModel = false;
                    selectedEntity->reloadRigidBody();
                }
            } else {
                if (ImGui::SliderFloat("##RotationY", &selectedEntity->rotation.y, -180.0f, 180.0f, "%.3f")) {
                    selectedEntity->reloadRigidBody();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    EntityRotationYInputModel = true;
                }
            }
            ImGui::PopID();

            // Rotation Z
            ImGui::PushID("TRANSFORM-ROTATION-Z-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Rotation Z");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (EntityRotationZInputModel) {
                if (ImGui::InputFloat("##RotationZ", &selectedEntity->rotation.z, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                    EntityRotationZInputModel = false;
                    selectedEntity->reloadRigidBody();
                }
            } else {
                if (ImGui::SliderFloat("##RotationZ", &selectedEntity->rotation.z, -180.0f, 180.0f, "%.3f")) {
                    selectedEntity->reloadRigidBody();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    EntityRotationZInputModel = true;
                }
            }
            ImGui::PopID();

            selectedEntity->setRot(selectedEntity->rotation);

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    // --- Assets Section ---
    constexpr const char* assets_cstr = ICON_FA_CUBE " Assets";
    if (ImGui::CollapsingHeader(assets_cstr)) {
        ImGui::Indent(20.0f);
        if (ImGui::BeginTable("Assets", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            // Model
            ImGui::PushID("ASSETS-MODEL-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Model");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string modelName = selectedEntity->modelPath.empty() ? "Drag Model Here" : selectedEntity->modelPath.stem().string();
            ImGui::Button(modelName.c_str(), ImVec2(-FLT_MIN, 25));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int payloadIndex = *(const int*)payload->Data;
                    fs::path path = dirPath / allItems[payloadIndex].name;
                    selectedEntity->modelPath = path;
                    selectedEntity->setModel(selectedEntity->modelPath.c_str());
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();

            // Material
            ImGui::PushID("ASSETS-MATERIAL-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Material");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string materialName = selectedEntity->childMaterialPath.empty() ? "Drag Material Here" : selectedEntity->childMaterialPath.stem().string();
            ImGui::Button(materialName.c_str(), ImVec2(-FLT_MIN, 25));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_PAYLOAD")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    const int payloadIndex = *(const int*)payload->Data;
                    const fs::path path = allItems[payloadIndex].path;
                    if (!childMaterials.contains(path)) {
                        LoadChildMaterial(path);
                    }
                    if (childMaterials.contains(path)) {
                        selectedEntity->childMaterialPath = path;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();

            // Script
            ImGui::PushID("ASSETS-SCRIPT-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Script");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string scriptName = selectedEntity->scriptPath.empty() ? "Drag Script Here" : selectedEntity->scriptPath.filename().string();
            float buttonWidth = ImGui::GetContentRegionAvail().x - 30.0f;
            ImGui::Button(scriptName.c_str(), ImVec2(buttonWidth, 25));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD")) {
                    IM_ASSERT(payload->DataSize == sizeof(int));
                    int payload_n = *(const int*)payload->Data;
                    fs::path path = dirPath / allItems[payload_n].name;
                    selectedEntity->scriptPath = path;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "##ClearScript", ImVec2(25, 25))) {
                selectedEntity->scriptPath.clear();
            }
            ImGui::PopID();

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    constexpr const char* material_cstr = ICON_FA_BRUSH " Material";
    if (ImGui::CollapsingHeader(material_cstr)) {
        ImGui::Indent(20.0f);

        if (!selectedEntity->childMaterialPath.empty()) {
            auto it = childMaterials.find(selectedEntity->childMaterialPath);
            if (it != childMaterials.end()) {
                MaterialInspector(it->second);
            }
        } else {
            constexpr const char* info_text = "No material assigned. Drag a material asset here from the Assets Explorer to apply it.";
            ImGui::DrawMessageBox(info_text, MessageBoxType::Info);
        }

        ImGui::Unindent(20.0f);
    }

    // --- Physics Section ---
    constexpr const char* physics_cstr = ICON_FA_GLOBE " Physics";
    if (ImGui::CollapsingHeader(physics_cstr)) {
        ImGui::Indent(20.0f);
        if (ImGui::BeginTable("Physics", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 80.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            // Is Dynamic
            ImGui::PushID("PHYSICS-DYNAMIC-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Dynamic");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            bool isDynamic = selectedEntity->getFlag(Entity::Flag::IS_DYNAMIC);
            if (ImGui::ToggleButton("##isDynamic", isDynamic)) {
                selectedEntity->setFlag(Entity::Flag::IS_DYNAMIC, isDynamic);
                if (isDynamic) selectedEntity->makePhysicsDynamic();
                else selectedEntity->makePhysicsStatic();
            }
            ImGui::PopID();

            // Collision Shape
            ImGui::PushID("PHYSICS-SHAPE-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Shape");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char* collisionShapeNames[] = {"Box", "HighPolyMesh", "None"};
            int currentItem = static_cast<int>(selectedEntity->currentCollisionShapeType);
            if (ImGui::BeginCombo("##CollisionType", collisionShapeNames[currentItem])) {
                for (int i = 0; i < IM_ARRAYSIZE(collisionShapeNames); i++) {
                    const bool isSelected = (currentItem == i);
                    if (ImGui::Selectable(collisionShapeNames[i], isSelected)) {
                        selectedEntity->currentCollisionShapeType = static_cast<CollisionShapeType>(i);
                        selectedEntity->reloadRigidBody();
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopID();

            // Mass
            ImGui::PushID("PHYSICS-MASS-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Mass");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##Mass", &selectedEntity->mass, 0.1f, 0.0f, 1000.0f, "%.1f kg")) {
                selectedEntity->updateMass();
            }
            ImGui::PopID();

            // Friction
            ImGui::PushID("PHYSICS-FRICTION-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Friction");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##Friction", &selectedEntity->friction, 0.05f, 0.0f, 10.0f, "%.2f")) {
                selectedEntity->setFriction(selectedEntity->friction);
            }
            ImGui::PopID();

            // Damping
            ImGui::PushID("PHYSICS-DAMPING-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Damping");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##Damping", &selectedEntity->damping, 0.05f, 0.0f, 5.0f, "%.2f")) {
                selectedEntity->applyDamping(selectedEntity->damping);
            }
            ImGui::PopID();

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    // --- Properties Section ---
    constexpr const char* properties_cstr = ICON_FA_LIST_CHECK " Properties";
    if (ImGui::CollapsingHeader(properties_cstr)) {
        ImGui::Indent(20.0f);
        if (ImGui::BeginTable("Properties", 2, ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
            ImGui::Indent(10.0f);

            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentEnable, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            // Visible
            ImGui::PushID("PROPS-VISIBLE-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Visible");
            ImGui::TableSetColumnIndex(1);
            bool isVisible = selectedEntity->getFlag(Entity::Flag::VISIBLE);
            if (ImGui::ToggleButton("##Visible", isVisible)) {
                selectedEntity->setFlag(Entity::Flag::VISIBLE, isVisible);
            }
            ImGui::PopID();

            // Has Collider
            ImGui::PushID("PROPS-COLLIDER-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Enable Collision");
            ImGui::TableSetColumnIndex(1);
            bool hasCollider = selectedEntity->getFlag(Entity::Flag::COLLIDER);
            if (ImGui::ToggleButton("##Collisions", hasCollider)) {
                selectedEntity->setFlag(Entity::Flag::COLLIDER, hasCollider);
            }
            ImGui::PopID();

            // Level of Detail
            ImGui::PushID("PROPS-LOD-ROW");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Level of Detail");
            ImGui::TableSetColumnIndex(1);
            bool lodEnabled = selectedEntity->getFlag(Entity::Flag::LOD_ENABLED);
            if (ImGui::ToggleButton("##Lod", lodEnabled)) {
                selectedEntity->setFlag(Entity::Flag::LOD_ENABLED, lodEnabled);
            }
            ImGui::PopID();

            ImGui::PopStyleVar();
            ImGui::Unindent(10.0f);
            ImGui::EndTable();
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::Spacing();

    // --- Advanced Settings Section ---
    constexpr const char* advanced_cstr = ICON_FA_COG_UTF8 " Advanced";
    if (ImGui::CollapsingHeader(advanced_cstr)) {
        ImGui::Indent(20.0f);
        constexpr char* warning_text = "These settings are experimental. They will not be saved with the project and may cause unexpected behavior.";
        ImGui::DrawMessageBox(warning_text, MessageBoxType::Warning);
        ImGui::Spacing();
        if (ImGui::Button("Set all children instanced", ImVec2(-FLT_MIN, 0))) {
            selectedEntity->makeChildrenInstances();
        }
        ImGui::Unindent(20.0f);
    }

    ImGui::PopStyleVar();
}