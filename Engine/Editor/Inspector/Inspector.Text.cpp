#include "../../include_all.h"

void TextInspector()
{
    ImGui::Text("Inspecting Text");
    
    if (std::holds_alternative<Text*>(object_in_inspector)) {
        selected_textElement = std::get<Text*>(object_in_inspector);
    }

    if (!selected_textElement)
        return;

    // Label width (adjust as needed)
    const float labelWidth = 100.0f;

    ImGui::Text("Name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    size_t buffer_size = selected_textElement->name.length() + 100;
    char name_buffer[buffer_size];

    strncpy(name_buffer, selected_textElement->name.c_str(), buffer_size - 1);
    name_buffer[buffer_size - 1] = '\0';

    // Input field width (adjust as needed)
    const float inputWidth = 200.0f;

    if (ImGui::InputText("##Name", name_buffer, IM_ARRAYSIZE(name_buffer)))
    {
        selected_textElement->name = name_buffer;
    }

    ImGui::Text("Text:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    size_t buffer_size = selected_textElement->text.length() + 100;
    char text_buffer[buffer_size];

    // Ensure null-termination
    strncpy(text_buffer, selected_textElement->text.c_str(), buffer_size - 1);
    text_buffer[buffer_size - 1] = '\0';

    if (ImGui::InputTextMultiline("##Text", text_buffer, IM_ARRAYSIZE(text_buffer), ImVec2(-1, 115)))
    {
        selected_textElement->text = text_buffer;
    }

    ImGui::Text("Font Size:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!FontSizeActiveInputMode) {
        ImGui::SliderFloat("##Font Size", &selected_textElement->fontSize, 0.0f, 100.0f);
        FontSizeActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Font Size", &selected_textElement->fontSize, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            FontSizeActiveInputMode = false;
    }


    ImGui::Text("Spacing:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextSpacingActiveInputMode) {
        ImGui::SliderFloat("##Text Spacing", &selected_textElement->spacing, 0.0f, 100.0f);
        TextSpacingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Text Spacing", &selected_textElement->spacing, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextSpacingActiveInputMode = false;
    }


    ImGui::Text("BG Roundiness:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextBackgroundRoundinessActiveInputMode) {
        ImGui::SliderFloat("##Background Roundiness", &selected_textElement->backgroundRoundness, 0.0f, 10.0f);
        TextBackgroundRoundinessActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Background Roundiness", &selected_textElement->backgroundRoundness, 0.0f, 10.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextBackgroundRoundinessActiveInputMode = false;
    }


    ImGui::Text("Padding:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextPaddingActiveInputMode) {
        ImGui::SliderFloat("##Text Padding", &selected_textElement->padding, 0.0f, 100.0f);
        TextPaddingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Text Padding", &selected_textElement->padding, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextPaddingActiveInputMode = false;
    }


    ImGui::Text("Color: ");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    ImVec4 text_colorImGui = ImVec4(
        selected_textElement->color.r / 255.0f,
        selected_textElement->color.g / 255.0f,
        selected_textElement->color.b / 255.0f,
        selected_textElement->color.a / 255.0f
    );

    // ImGui::ColorEdit4 will continuously update text_colorImGui while dragging
    bool colorChanged = ImGui::ColorEdit4("##Text Color", (float*)&text_colorImGui, ImGuiColorEditFlags_NoInputs);

    Color text_color = {
        (unsigned char)(text_colorImGui.x * 255.0f),
        (unsigned char)(text_colorImGui.y * 255.0f),
        (unsigned char)(text_colorImGui.z * 255.0f),
        (unsigned char)(text_colorImGui.w * 255.0f)
    };

    if (colorChanged) {
        selected_textElement->color = text_color;
    }




    ImGui::Text("BG Color: ");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    ImVec4 textBG_colorImGui = ImVec4(
        selected_textElement->backgroundColor.r / 255.0f,
        selected_textElement->backgroundColor.g / 255.0f,
        selected_textElement->backgroundColor.b / 255.0f,
        selected_textElement->backgroundColor.a / 255.0f
    );

    bool bgColorChanged = ImGui::ColorEdit4("##Text BG Color", (float*)&textBG_colorImGui, ImGuiColorEditFlags_NoInputs);

    Color textBG_color = {
        (unsigned char)(textBG_colorImGui.x * 255.0f),
        (unsigned char)(textBG_colorImGui.y * 255.0f),
        (unsigned char)(textBG_colorImGui.z * 255.0f),
        (unsigned char)(textBG_colorImGui.w * 255.0f)
    };

    if (bgColorChanged) {
        selected_textElement->backgroundColor = textBG_color;
    }
}
