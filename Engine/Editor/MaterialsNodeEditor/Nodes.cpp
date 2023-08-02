#pragma once
#include "MaterialsNodeEditor.h"

int TextureNode(MyNode* node);
int ColorNode(MyNode* node);
int MaterialNode(MyNode* node);

int TextureNode(MyNode* node)
{

    if (node->Title != "Diffuse Texture")
        return;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
    {
        if (outputSlot.title != "Diffuse Texture")
            return;
    
        ImGui::ImageButton((ImTextureID)&selected_entity->texture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            entity_material.texture = LoadTexture(path.c_str());
            entity_material.texture_path = path;
        }
        ImGui::EndDragDropTarget();

    }
}

int NormalMapTextureNode(MyNode* node)
{

    if (node->Title != "Normal Map Texture")
        return;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
    {
        if (outputSlot.title != "Normal Map Texture")
            return;


        ImGui::ImageButton((ImTextureID)&selected_entity->normal_texture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget())
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            entity_material.normal_texture = LoadTexture(path.c_str());
            entity_material.normal_texture_path = path;
        }
        ImGui::EndDragDropTarget();

    }
}

int ColorNode(MyNode* node)
{
    if (node->Title == "Color")
    {
        ImGui::SetNextItemWidth(300);
        ImGui::ColorEdit4("Color Picker", (float*)&node->ColorValue);
    }
}

int MaterialNode(MyNode* node)
{
    if (node->Title != "Material")
        return;

    can_apply_material = true;
    for (const Connection& connection : node->Connections)
    {
        if (connection.InputSlot == "Color")
        {
            ImVec4 colorValue = ((MyNode*)(connection.OutputNode))->ColorValue;
            ImGui::ColorButton("Connected Color", colorValue, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip);
            entity_material.color = { colorValue.x * 255, colorValue.y * 255, colorValue.z * 255, colorValue.w * 255 };
        }
    }
}