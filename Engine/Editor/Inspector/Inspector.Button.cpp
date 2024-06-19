void ButtonInspector()
{
    ImGui::Text("Inspecting Button");

    if (!selectedButton)
        return;

    float labelWidth = 100.0f;

    ImGui::Text("Name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

//    size_t bufferSize = selectedButton->name.length() + 100;
    char name_buffer[255];
    size_t bufferSize = sizeof(name_buffer);

    strncpy(name_buffer, selectedButton->name.c_str(), bufferSize - 1);
    name_buffer[bufferSize - 1] = '\0';
    
    const float inputWidth = 200.0f;

    if (ImGui::InputText("##Name", name_buffer, IM_ARRAYSIZE(name_buffer)))
    {
        selectedButton->name = name_buffer;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Button Proprieties"))
    {
        ImGui::Indent(10.0f);

        ImGui::Text("Enabled:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##Enabled", &selectedButton->enabled);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Auto-resize:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##AutoResize", &selectedButton->autoResize);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        if (selectedButton->autoResize)
            ImGui::BeginDisabled();

        ImGui::Text("Size:");
        ImGui::Text("X:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Scale Name X"); 
        ImGui::SliderFloat("##Scale X", &selectedButton->size.x, 0, GetScreenWidth());
        ImGui::PopID(); 

        ImGui::Text("Y:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Scale Name Y"); 
        ImGui::SliderFloat("##Scale Y", &selectedButton->size.y, 0, GetScreenHeight());
        ImGui::PopID();

        if (selectedButton->autoResize)
            ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Position:");
        ImGui::Text("X:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name X"); 
        ImGui::SliderFloat("##Position X", &selectedButton->position.x, -(GetScreenWidth()*2), GetScreenWidth()*2);
        ImGui::PopID(); 

        ImGui::Text("Y:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name Y"); 
        ImGui::SliderFloat("##Position Y", &selectedButton->position.y, -(GetScreenHeight()*2), GetScreenHeight()*2);
        ImGui::PopID(); 

        ImGui::Text("Z-Index:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name Z"); 
        ImGui::SliderFloat("##Position Z", &selectedButton->position.z, -(500), 500);
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Roundness:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!ButtonRoundnessActiveInputMode) {
            ImGui::SliderFloat("##Button Roundiness", &selectedButton->roundness, 0.0f, 10.0f);
            ButtonRoundnessActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Button Roundiness", &selectedButton->roundness, 0.0f, 10.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                ButtonRoundnessActiveInputMode = false;
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        if (ImGui::CollapsingHeader("Color"))
        {
            ImGui::Indent(30.0f);
            labelWidth += 30;
            // Button Color
            {

                ImGui::Text("Button Color: ");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);

                ImVec4 defaultButtonColorImGui = ImVec4(
                    selectedButton->color.r / 255.0f,
                    selectedButton->color.g / 255.0f,
                    selectedButton->color.b / 255.0f,
                    selectedButton->color.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Default Button Color", (float*)&defaultButtonColorImGui, ImGuiColorEditFlags_NoInputs);

                Color defaultButtonColor = {
                    (unsigned char)(defaultButtonColorImGui.x * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.y * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.z * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.w * 255.0f)
                };

                selectedButton->color = defaultButtonColor;
            }
            
            // Button Hover Color
            {
                ImGui::Text("Hover Color: ");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);

                ImVec4 buttonHoverColorImGui = ImVec4(
                    selectedButton->hoverColor.r / 255.0f,
                    selectedButton->hoverColor.g / 255.0f,
                    selectedButton->hoverColor.b / 255.0f,
                    selectedButton->hoverColor.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Button Hover Color", (float*)&buttonHoverColorImGui, ImGuiColorEditFlags_NoInputs);

                Color buttonHoverColor = {
                    (unsigned char)(buttonHoverColorImGui.x * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.y * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.z * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.w * 255.0f)
                };

                selectedButton->hoverColor = buttonHoverColor;

            }

            // Button Pressed Color
            {
                ImGui::Text("Pressed Color: ");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);

                ImVec4 buttonPressedColorImGui = ImVec4(
                    selectedButton->pressedColor.r / 255.0f,
                    selectedButton->pressedColor.g / 255.0f,
                    selectedButton->pressedColor.b / 255.0f,
                    selectedButton->pressedColor.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Button Pressed Color", (float*)&buttonPressedColorImGui, ImGuiColorEditFlags_NoInputs);

                Color buttonPressedColor = {
                    (unsigned char)(buttonPressedColorImGui.x * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.y * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.z * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.w * 255.0f)
                };

                selectedButton->pressedColor = buttonPressedColor;

            }


            labelWidth -= 30;
            ImGui::Unindent(30.0f);

        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Unindent(10.0f);

    }

    if (ImGui::CollapsingHeader("Text Proprieties"))
    {
        ImGui::Text("Text:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        size_t bufferSize = selectedButton->text.text.length() + 100;
        char text_buffer[bufferSize];

        strncpy(text_buffer, selectedButton->text.text.c_str(), bufferSize - 1);
        text_buffer[bufferSize - 1] = '\0';

        if (ImGui::InputTextMultiline("##Text", text_buffer, IM_ARRAYSIZE(text_buffer), ImVec2(-1, 115)))
        {
            selectedButton->text.text = text_buffer;
        }

        ImGui::Text("Font Size:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!FontSizeActiveInputMode) {
            ImGui::SliderFloat("##Font Size", &selectedButton->text.fontSize, 0.0f, 100.0f);
            FontSizeActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Font Size", &selectedButton->text.fontSize, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                FontSizeActiveInputMode = false;
        }


        ImGui::Text("Spacing:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!TextSpacingActiveInputMode) {
            ImGui::SliderFloat("##Text Spacing", &selectedButton->text.spacing, 0.0f, 100.0f);
            TextSpacingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Text Spacing", &selectedButton->text.spacing, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                TextSpacingActiveInputMode = false;
        }




        ImGui::Text("Color: ");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        ImVec4 text_colorImGui = ImVec4(
            selectedButton->text.color.r / 255.0f,
            selectedButton->text.color.g / 255.0f,
            selectedButton->text.color.b / 255.0f,
            selectedButton->text.color.a / 255.0f
        );

        
        bool colorChanged = ImGui::ColorEdit4("##Text Color", (float*)&text_colorImGui, ImGuiColorEditFlags_NoInputs);

        Color text_color = {
            (unsigned char)(text_colorImGui.x * 255.0f),
            (unsigned char)(text_colorImGui.y * 255.0f),
            (unsigned char)(text_colorImGui.z * 255.0f),
            (unsigned char)(text_colorImGui.w * 255.0f)
        };

        if (colorChanged) {
            selectedButton->text.color = text_color;
        }




        selectedButton->text.backgroundColor.a = 0;

    }
}
