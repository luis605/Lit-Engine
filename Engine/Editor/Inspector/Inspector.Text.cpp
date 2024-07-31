void TextInspector()
{
    ImGui::Text("Inspecting Text");

    if (!selectedTextElement) return;

    const float labelWidth = 100.0f;
    const float inputWidth = 200.0f;

    ImGui::Text("Name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    size_t bufferSize = selectedTextElement->name.length() + 100;
    char name_buffer[bufferSize];

    strncpy(name_buffer, selectedTextElement->name.c_str(), bufferSize - 1);
    name_buffer[bufferSize - 1] = '\0';

    if (ImGui::InputText("##Name", name_buffer, IM_ARRAYSIZE(name_buffer)))
        selectedTextElement->name = name_buffer;

    ImGui::Text("Position:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);
    float position[3] = { selectedTextElement->position.x, selectedTextElement->position.y, selectedTextElement->position.z };
    if (ImGui::InputFloat3("##Position", position))
        selectedTextElement->position = LitVector3{ position[0], position[1], position[2] };

    ImGui::Text("Text:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    bufferSize = selectedTextElement->text.length() + 100;
    char text_buffer[bufferSize];

    strncpy(text_buffer, selectedTextElement->text.c_str(), bufferSize - 1);
    text_buffer[bufferSize - 1] = '\0';

    if (ImGui::InputTextMultiline("##Text", text_buffer, IM_ARRAYSIZE(text_buffer), ImVec2(-1, 115)))
        selectedTextElement->text = text_buffer;

    ImGui::Text("Font Size:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!FontSizeActiveInputMode) {
        ImGui::SliderFloat("##Font Size", &selectedTextElement->fontSize, 0.0f, 100.0f);
        FontSizeActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Font Size", &selectedTextElement->fontSize, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            FontSizeActiveInputMode = false;
    }


    ImGui::Text("Spacing:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextSpacingActiveInputMode) {
        ImGui::SliderFloat("##Text Spacing", &selectedTextElement->spacing, 0.0f, 100.0f);
        TextSpacingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Text Spacing", &selectedTextElement->spacing, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextSpacingActiveInputMode = false;
    }


    ImGui::Text("BG Roundiness:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextBackgroundRoundinessActiveInputMode) {
        ImGui::SliderFloat("##Background Roundiness", &selectedTextElement->backgroundRoundness, 0.0f, 10.0f);
        TextBackgroundRoundinessActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Background Roundiness", &selectedTextElement->backgroundRoundness, 0.0f, 10.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextBackgroundRoundinessActiveInputMode = false;
    }


    ImGui::Text("Padding:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    if (!TextPaddingActiveInputMode) {
        ImGui::SliderFloat("##Text Padding", &selectedTextElement->padding, 0.0f, 100.0f);
        TextPaddingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
    }
    else
    {
        if (ImGui::InputFloat("##Text Padding", &selectedTextElement->padding, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
            TextPaddingActiveInputMode = false;
    }


    ImGui::Text("Color: ");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    ImVec4 text_colorImGui = ImVec4(
        selectedTextElement->color.r / 255.0f,
        selectedTextElement->color.g / 255.0f,
        selectedTextElement->color.b / 255.0f,
        selectedTextElement->color.a / 255.0f
    );

    bool colorChanged = ImGui::ColorEdit4("##Text Color", (float*)&text_colorImGui, ImGuiColorEditFlags_NoInputs);

    Color text_color = {
        (unsigned char)(text_colorImGui.x * 255.0f),
        (unsigned char)(text_colorImGui.y * 255.0f),
        (unsigned char)(text_colorImGui.z * 255.0f),
        (unsigned char)(text_colorImGui.w * 255.0f)
    };

    if (colorChanged) {
        selectedTextElement->color = text_color;
    }




    ImGui::Text("BG Color: ");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    ImVec4 textBG_colorImGui = ImVec4(
        selectedTextElement->backgroundColor.r / 255.0f,
        selectedTextElement->backgroundColor.g / 255.0f,
        selectedTextElement->backgroundColor.b / 255.0f,
        selectedTextElement->backgroundColor.a / 255.0f
    );

    bool bgColorChanged = ImGui::ColorEdit4("##Text BG Color", (float*)&textBG_colorImGui, ImGuiColorEditFlags_NoInputs);

    Color textBG_color = {
        (unsigned char)(textBG_colorImGui.x * 255.0f),
        (unsigned char)(textBG_colorImGui.y * 255.0f),
        (unsigned char)(textBG_colorImGui.z * 255.0f),
        (unsigned char)(textBG_colorImGui.w * 255.0f)
    };

    if (bgColorChanged) {
        selectedTextElement->backgroundColor = textBG_color;
    }
}
