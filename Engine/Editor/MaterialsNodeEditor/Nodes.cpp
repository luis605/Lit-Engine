#pragma once

int TextureNode(MyNode* node);
void ColorNode(MyNode* node);
int SurfaceMaterialNode(MyNode* node);
int MaterialNode(MyNode* node);

int TextureNode(MyNode* node) {
    if (node->Title != "Diffuse Texture") return -1;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots) {
        if (outputSlot.title != "Diffuse Texture") return -1;

        ImGui::ImageButton("", (ImTextureID)&selectedEntity->surfaceMaterial.diffuseTexture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget())
            return -1;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            fs::path path = dirPath / fileStruct[payload_n].name;

            entityMaterial.texture = LoadTexture((char*)path.c_str());
            entityMaterial.texturePath = path;
        }

        ImGui::EndDragDropTarget();
    }

    return 0;
}

int NormalMapTextureNode(MyNode* node) {
    if (node->Title != "Normal Map Texture") return -1;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots) {
        if (outputSlot.title != "Normal Map Texture") return -1;


        ImGui::ImageButton("", (ImTextureID)&selectedEntity->surfaceMaterial.normalTexture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget()) return -1;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD")) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            fs::path path = dirPath / fileStruct[payload_n].name;

            entityMaterial.normalTexture = LoadTexture(path.string().c_str());
            entityMaterial.normalTexturePath = path;
        }

        ImGui::EndDragDropTarget();
    }

    return 0;
}

void ColorNode(MyNode* node) {
    if (node->Title == "Color") {
        ImGui::SetNextItemWidth(300);
        ImGui::ColorEdit4("Color Picker", (float*)&node->ColorValue);
    }
}

int SurfaceMaterialNode(MyNode* node) {
    if (node->Title != "Surface Material") return -1;

    const float inputWidth = 200.0f;

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots) {
        ImGui::Text("Shininess");
        ImGui::SameLine();
        ImGui::SliderFloat("##Shininess", &entityMaterial.shininess, 0.0f, 100.0f);

        ImGui::Text("Specular Intensity");
        ImGui::SameLine();
        ImGui::SliderFloat("##SpecularIntensity", &entityMaterial.SpecularIntensity, 0.0f, 100.0f);

        ImGui::Text("Roughness");
        ImGui::SameLine();
        ImGui::SliderFloat("##Roughness", &entityMaterial.Roughness, 0.0f, 100.0f);

        ImGui::Text("Diffuse Intensity");
        ImGui::SameLine();
        ImGui::SliderFloat("##DiffuseIntensity", &entityMaterial.DiffuseIntensity, 0.0f, 1.0f);

        ImGui::Text("Specular Tint");
        ImGui::SameLine();
        ImVec4 specular_tint = {entityMaterial.SpecularTint.x, entityMaterial.SpecularTint.y, entityMaterial.SpecularTint.z, 1};
        ImGui::ColorEdit4("Color Picker", (float*)&specular_tint, ImGuiColorEditFlags_NoInputs);
        entityMaterial.SpecularTint = { specular_tint.x, specular_tint.y, specular_tint.z};
    }

    return 0;
}

int MaterialNode(MyNode* node) {
    if (node->Title != "Material") return -1;

    canApplyMaterial = true;
    for (const Connection& connection : node->Connections) {
        if (connection.InputSlot == "Color") {
            ImVec4 colorValue = ((MyNode*)(connection.OutputNode))->ColorValue;
            ImGui::ColorButton("Connected Color", colorValue, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip);
            entityMaterial.color = { colorValue.x, colorValue.y, colorValue.z, colorValue.w };
        }
    }

    return 0;
}