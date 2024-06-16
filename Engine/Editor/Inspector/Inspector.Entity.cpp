#include "../../../include_all.h"

void DisplayEntityNameInput() {
    std::string entityName = selectedEntity->name;
    char inputBuffer[255];
    size_t bufferSize = sizeof(inputBuffer);
    strncpy(inputBuffer, entityName.c_str(), bufferSize - 1);
    inputBuffer[bufferSize - 1] = '\0';

    ImGui::Text("Inspecting '");
    ImGui::SameLine();
    
    float textWidth = std::max(ImGui::CalcTextSize(entityName.c_str()).x + 10.0f, 100.0f);
    ImGui::SetNextItemWidth(textWidth);

    if (ImGui::InputText("##Title Part 2", inputBuffer, sizeof(inputBuffer))) {
        selectedEntity->name = inputBuffer;
    }

    ImGui::SameLine();
    ImGui::Text("'");
}

void DisplayTransformControls() {
    ImGui::Text("Scale:");
    ImGui::Indent(20.0f);

    if (ImGui::InputFloat3("##Scale", &selectedEntity->scale.x)) {
        selectedEntity->reloadRigidBody();
    }
    ImGui::Unindent(20.0f);

    ImGui::Text("Position:");
    ImGui::Indent(20.0f);

    ImGui::InputFloat3("##Position", &selectedEntityPosition.x);
    ImGui::Unindent(20.0f);

    if (selectedEntity->isChild) {
        selectedEntity->relativePosition = selectedEntityPosition;
    } else {
        if (selectedEntity->isDynamic && selectedEntity->calcPhysics) {
            selectedEntity->applyForce(selectedEntityPosition);
        } else {
            selectedEntity->position = selectedEntityPosition;
        }
    }

    ImGui::Text("Rotation:");
    ImGui::Indent(20.0f);

    ImGui::Text("X:");
    ImGui::SameLine();
    if (EntityRotationXInputModel) {
        if (ImGui::InputFloat("##RotationX", &selectedEntity->rotation.x, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            EntityRotationXInputModel = false;
            selectedEntity->reloadRigidBody();
        }
    } else {
        if (ImGui::SliderFloat("##RotationX", &selectedEntity->rotation.x, -180.0f, 180.0f, "%.3f"))
            selectedEntity->reloadRigidBody();

        EntityRotationXInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }

    ImGui::Text("Y:");
    ImGui::SameLine();
    if (EntityRotationYInputModel) {
        if (ImGui::InputFloat("##RotationY", &selectedEntity->rotation.y, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            selectedEntity->reloadRigidBody();
            EntityRotationYInputModel = false;
        }
    } else {
        if (ImGui::SliderFloat("##RotationY", &selectedEntity->rotation.y, -180.0f, 180.0f, "%.3f"))
            selectedEntity->reloadRigidBody();

        EntityRotationYInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }

    ImGui::Text("Z:");
    ImGui::SameLine();
    if (EntityRotationZInputModel) {
        if (ImGui::InputFloat("##RotationZ", &selectedEntity->rotation.z, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
            EntityRotationZInputModel = false;
            selectedEntity->reloadRigidBody();
        }
    } else {
        if (ImGui::SliderFloat("##RotationZ", &selectedEntity->rotation.z, -180.0f, 180.0f, "%.3f"))
            selectedEntity->reloadRigidBody();
            
        EntityRotationZInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    
    selectedEntity->setRot(selectedEntity->rotation);
    ImGui::Unindent(20.0f);
}

void DisplayModelDragDrop() {
    ImGui::Text("Model: ");
    ImGui::SameLine();
    if (ImGui::Button("##Drag'nDropModelPath", ImVec2(200, 25)));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payloadIndex = *(const int*)payload->Data;

            std::string path = dirPath.string() + "/" + filesTextureStruct[payloadIndex].name;
            selectedEntity->modelPath = path;
            selectedEntity->setModel(selectedEntity->modelPath.c_str());
        }
        ImGui::EndDragDropTarget();
    }
}

void DisplayMaterialDragDrop() {
    ImGui::Text("Material:");
    ImGui::SameLine();

    if (ImGui::Button("##Drag'nDropMaterialPath", ImVec2(200, 25)));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payloadIndex = *(const int*)payload->Data;

            std::string path = dirPath.string();
            path += "/" + filesTextureStruct[payloadIndex].name;

            selectedEntity->surfaceMaterialPath = path;
            DeserializeMaterial(&selectedEntity->surfaceMaterial, selectedEntity->surfaceMaterialPath.string().c_str());
        }
        ImGui::EndDragDropTarget();
    }

    if (!selectedEntity->surfaceMaterialPath.empty())
        MaterialInspector(&selectedEntity->surfaceMaterial, selectedEntity->surfaceMaterialPath.string());
}

void DisplayScriptDragDrop() {
    ImGui::Text("Script: ");
    ImGui::SameLine();
    
    const char* scriptName = selectedEntity->script.empty() ? "##ScriptPath" : selectedEntity->script.c_str();
    ImGui::Button(scriptName, ImVec2(200, 25));

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            std::string path = dirPath.string() + "/" + filesTextureStruct[payload_n].name;
            selectedEntity->script = path;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    if (ImGui::Button("x##ScriptEmptyButton", ImVec2(25, 25))) {
        selectedEntity->script.clear();
    }
}

void DisplayPhysicsSettings() {
    ImGui::Text("Collision Type");
    ImGui::SameLine();
    
    const char* collisionShapeNames[] = {"Box", "HighPolyMesh", "None"};
    int currentItem = static_cast<int>(*selectedEntity->currentCollisionShapeType);

    if (ImGui::BeginCombo("##CollisionType", collisionShapeNames[currentItem])) {
        for (int i = 0; i < IM_ARRAYSIZE(collisionShapeNames); i++) {
            const bool isSelected = (currentItem == i);

            if (ImGui::Selectable(collisionShapeNames[i], isSelected)) {
                *selectedEntity->currentCollisionShapeType = static_cast<Entity::CollisionShapeType>(i);
                selectedEntity->reloadRigidBody();
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0.0f, 15.0f));

    ImGui::Text("Is Dynamic");
    ImGui::SameLine();
    if (ImGui::Checkbox("##doPhysics", &selectedEntity->isDynamic)) {
        if (selectedEntity->isDynamic)
            selectedEntity->makePhysicsDynamic();
        else
            selectedEntity->makePhysicsStatic();
    }

    ImGui::Dummy(ImVec2(0.0f, 15.0f));

    ImGui::Text("Mass:");
    ImGui::SameLine();
    if (ImGui::SliderFloat("##Mass", &selectedEntity->mass, 0.0f, 100.0f, "%.1f")) {
        selectedEntity->updateMass();
    }

    ImGui::Text("Friction:");
    ImGui::SameLine();
    if (ImGui::SliderFloat("##Friction", &selectedEntity->friction, 0.0f, 10.0f, "%.1f")) {
        selectedEntity->setFriction(selectedEntity->friction);
    }

    ImGui::Text("Damping:");
    ImGui::SameLine();
    if (ImGui::SliderFloat("##Damping", &selectedEntity->damping, 0.0f, 5.0f, "%.1f")) {
        selectedEntity->applyDamping(selectedEntity->damping);
    }
}

void EntityInspector() {
    ImVec2 windowSize = ImGui::GetWindowSize();
    if (selectedEntity == nullptr || !selectedEntity->initialized) return;

    selectedEntityPosition = selectedEntity->isChild ? selectedEntity->relativePosition : selectedEntity->position;
    selectedEntityScale = selectedEntity->scale;

    DisplayEntityNameInput();
    ImGui::Dummy(ImVec2(0.0f, 15.0f));

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Entity Properties")) {
        ImGui::Indent(30.0f);
        DisplayModelDragDrop();
        DisplayTransformControls();
        ImGui::Unindent(30.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader(ICON_FA_CIRCLE_NOTCH " Materials")) {
        ImGui::Indent(30.0f);
        DisplayMaterialDragDrop();
        ImGui::Unindent(30.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader(ICON_FA_GLOBE " Physics")) {
        ImGui::Indent(30.0f);
        DisplayPhysicsSettings();
        ImGui::Unindent(30.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader(ICON_FA_COG " Advanced Settings")) {
        ImGui::Indent(30.0f);
        ImGui::TextWrapped("Warning!\nThese experimental features won't save or load.\nAvoid Advanced Settings until the alpha state.");

        if (ImGui::Button("Set all children instanced")) {
            selectedEntity->makeChildrenInstances();
        }
        ImGui::Unindent(30.0f);
    }
}
