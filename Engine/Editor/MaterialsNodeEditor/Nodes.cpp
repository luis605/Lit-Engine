#pragma once
#include "MaterialsNodeEditor.h"
#include "../../../include_all.h"

int TextureNode(MyNode* node);
int ColorNode(MyNode* node);
int SurfaceMaterialNode(MyNode* node);
int MaterialNode(MyNode* node);

int TextureNode(MyNode* node)
{

    if (node->Title != "Diffuse Texture")
        return -1;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
    {
        if (outputSlot.title != "Diffuse Texture")
            return -1;
    
        ImGui::ImageButton((ImTextureID)&selected_entity->texture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget())
            return -1;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.string();
            path += "/" + files_texture_struct[payload_n].name;

            entity_material.texture = LoadTexture((char*)path.c_str());
            entity_material.texture_path = path;
        }
        ImGui::EndDragDropTarget();

    }
}

int NormalMapTextureNode(MyNode* node)
{

    if (node->Title != "Normal Map Texture")
        return -1;

    ImVec2 initialCursorPos = ImGui::GetCursorPos();

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
    {
        if (outputSlot.title != "Normal Map Texture")
            return -1;


        ImGui::ImageButton((ImTextureID)&selected_entity->normal_texture, ImVec2(100, 100));

        if (!ImGui::BeginDragDropTarget())
            return -1;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.string();
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

int SurfaceMaterialNode(MyNode* node)
{
    if (node->Title != "Surface Material")
        return -1;

    const float inputWidth = 200.0f;

    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
    {

        ImGui::Text("Shininess");
        ImGui::SameLine(inputWidth);
        ImGui::SliderFloat("##Shininess", &entity_material.shininess, 0.0f, 100.0f);
        
        ImGui::Text("Specular Intensity");
        ImGui::SameLine(inputWidth);
        ImGui::SliderFloat("##SpecularIntensity", &entity_material.SpecularIntensity, 0.0f, 100.0f);
        
        ImGui::Text("Roughness");
        ImGui::SameLine(inputWidth);
        ImGui::SliderFloat("##Roughness", &entity_material.Roughness, 0.0f, 100.0f);
        
        ImGui::Text("Diffuse Intensity");
        ImGui::SameLine(inputWidth);
        ImGui::SliderFloat("##DiffuseIntensity", &entity_material.DiffuseIntensity, 0.0f, 1.0f);
    
        ImGui::Text("Specular Tint");
        ImGui::SameLine(inputWidth);
        ImVec4 specular_tint = {entity_material.SpecularTint.x, entity_material.SpecularTint.y, entity_material.SpecularTint.z, 1};
        ImGui::ColorEdit4("Color Picker", (float*)&specular_tint, ImGuiColorEditFlags_NoInputs);
        entity_material.SpecularTint = { specular_tint.x, specular_tint.y, specular_tint.z};
    }
}

int MaterialNode(MyNode* node)
{
    if (node->Title != "Material")
        return -1;

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