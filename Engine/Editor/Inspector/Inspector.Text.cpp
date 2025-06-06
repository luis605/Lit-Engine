/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <cstddef>

void TextInspector() {
    ImGui::Text("Inspecting Text");

    if (!selectedTextElement)
        return;

    const float labelWidth = 100.0f;
    const float inputWidth = 200.0f;

    ImGui::Text("Name:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    size_t nameBufferSize = selectedTextElement->name.length() + 100;
    std::vector<char> name_buffer(nameBufferSize);

    strncpy(name_buffer.data(), selectedTextElement->name.c_str(), nameBufferSize - 1);
    name_buffer[nameBufferSize - 1] = '\0';

    if (ImGui::InputText("##Name", name_buffer.data(), static_cast<int>(name_buffer.size()))) {
        selectedTextElement->name = name_buffer.data();
    }

    ImGui::Text("Position:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    float position[3] = {selectedTextElement->position.x,
                         selectedTextElement->position.y,
                         selectedTextElement->position.z};
    if (ImGui::InputFloat3("##Position", position))
        selectedTextElement->position =
            LitVector3{position[0], position[1], position[2]};

    ImGui::Text("Text:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    size_t textBufferSize = selectedTextElement->text.length() + 100;
    std::vector<char> text_buffer(textBufferSize);

    strncpy(text_buffer.data(), selectedTextElement->text.c_str(), textBufferSize - 1);
    text_buffer[textBufferSize - 1] = '\0';

    if (ImGui::InputTextMultiline("##Text", text_buffer.data(),
        static_cast<int>(text_buffer.size()), ImVec2(-1, 115))) {
        selectedTextElement->text = text_buffer.data();
    }


    ImGui::Text("Font Size:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    if (!FontSizeActiveInputMode) {
        ImGui::SliderFloat("##Font Size", &selectedTextElement->fontSize, 0.0f,
                           100.0f);
        FontSizeActiveInputMode =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    } else {
        if (ImGui::InputFloat("##Font Size", &selectedTextElement->fontSize,
                              0.0f, 100.0f, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            FontSizeActiveInputMode = false;
    }

    ImGui::Text("Spacing:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    if (!TextSpacingActiveInputMode) {
        ImGui::SliderFloat("##Text Spacing", &selectedTextElement->spacing,
                           0.0f, 100.0f);
        TextSpacingActiveInputMode =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    } else {
        if (ImGui::InputFloat("##Text Spacing", &selectedTextElement->spacing,
                              0.0f, 100.0f, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            TextSpacingActiveInputMode = false;
    }

    ImGui::Text("BG Roundiness:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    if (!TextBackgroundRoundinessActiveInputMode) {
        ImGui::SliderFloat("##Background Roundiness",
                           &selectedTextElement->backgroundRoundness, 0.0f,
                           10.0f);
        TextBackgroundRoundinessActiveInputMode =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    } else {
        if (ImGui::InputFloat("##Background Roundiness",
                              &selectedTextElement->backgroundRoundness, 0.0f,
                              10.0f, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            TextBackgroundRoundinessActiveInputMode = false;
    }

    ImGui::Text("Padding:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    if (!TextPaddingActiveInputMode) {
        ImGui::SliderFloat("##Text Padding", &selectedTextElement->padding,
                           0.0f, 100.0f);
        TextPaddingActiveInputMode =
            ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    } else {
        if (ImGui::InputFloat("##Text Padding", &selectedTextElement->padding,
                              0.0f, 100.0f, "%.3f",
                              ImGuiInputTextFlags_EnterReturnsTrue))
            TextPaddingActiveInputMode = false;
    }

    ImGui::Text("Color: ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    ImVec4 text_colorImGui = ImVec4(selectedTextElement->color.r / 255.0f,
                                    selectedTextElement->color.g / 255.0f,
                                    selectedTextElement->color.b / 255.0f,
                                    selectedTextElement->color.a / 255.0f);

    bool colorChanged = ImGui::ColorEdit4(
        "##Text Color", (float*)&text_colorImGui, ImGuiColorEditFlags_NoInputs);

    Color text_color = {(unsigned char)(text_colorImGui.x * 255.0f),
                        (unsigned char)(text_colorImGui.y * 255.0f),
                        (unsigned char)(text_colorImGui.z * 255.0f),
                        (unsigned char)(text_colorImGui.w * 255.0f)};

    if (colorChanged) {
        selectedTextElement->color = text_color;
    }

    ImGui::Text("BG Color: ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);

    ImVec4 textBG_colorImGui =
        ImVec4(selectedTextElement->backgroundColor.r / 255.0f,
               selectedTextElement->backgroundColor.g / 255.0f,
               selectedTextElement->backgroundColor.b / 255.0f,
               selectedTextElement->backgroundColor.a / 255.0f);

    bool bgColorChanged =
        ImGui::ColorEdit4("##Text BG Color", (float*)&textBG_colorImGui,
                          ImGuiColorEditFlags_NoInputs);

    Color textBG_color = {(unsigned char)(textBG_colorImGui.x * 255.0f),
                          (unsigned char)(textBG_colorImGui.y * 255.0f),
                          (unsigned char)(textBG_colorImGui.z * 255.0f),
                          (unsigned char)(textBG_colorImGui.w * 255.0f)};

    if (bgColorChanged) {
        selectedTextElement->backgroundColor = textBG_color;
    }
}
