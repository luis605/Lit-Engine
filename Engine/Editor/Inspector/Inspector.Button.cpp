#include "../../include_all.h"

void ButtonInspector()
{
    ImGui::Text("Inspecting Button");
    
    if (std::holds_alternative<LitButton*>(object_in_inspector)) {
        selected_button = std::get<LitButton*>(object_in_inspector);
    }

    if (!selected_button)
        return;

    float labelWidth = 100.0f;

    ImGui::Text("Name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1);

    size_t buffer_size = selected_button->name.length() + 100;
    char name_buffer[buffer_size];

    strncpy(name_buffer, selected_button->name.c_str(), buffer_size - 1);
    name_buffer[buffer_size - 1] = '\0';
    
    const float inputWidth = 200.0f;

    if (ImGui::InputText("##Name", name_buffer, IM_ARRAYSIZE(name_buffer)))
    {
        selected_button->name = name_buffer;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Button Proprieties"))
    {
        ImGui::Indent(10.0f);

        ImGui::Text("Enabled:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##Enabled", &selected_button->enabled);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Auto-resize:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::Checkbox("##AutoResize", &selected_button->autoResize);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        if (selected_button->autoResize)
            ImGui::BeginDisabled();

        ImGui::Text("Size:");
        ImGui::Text("X:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Scale Name X"); 
        ImGui::SliderFloat("##Scale X", &selected_button->size.x, 0, GetScreenWidth());
        ImGui::PopID(); 

        ImGui::Text("Y:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Scale Name Y"); 
        ImGui::SliderFloat("##Scale Y", &selected_button->size.y, 0, GetScreenHeight());
        ImGui::PopID();

        if (selected_button->autoResize)
            ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Position:");
        ImGui::Text("X:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name X"); 
        ImGui::SliderFloat("##Position X", &selected_button->position.x, -(GetScreenWidth()*2), GetScreenWidth()*2);
        ImGui::PopID(); 

        ImGui::Text("Y:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name Y"); 
        ImGui::SliderFloat("##Position Y", &selected_button->position.y, -(GetScreenHeight()*2), GetScreenHeight()*2);
        ImGui::PopID(); 

        ImGui::Text("Z-Index:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("##Position Name Z"); 
        ImGui::SliderFloat("##Position Z", &selected_button->position.z, -(500), 500);
        ImGui::PopID();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Roundness:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!ButtonRoundnessActiveInputMode) {
            ImGui::SliderFloat("##Button Roundiness", &selected_button->roundness, 0.0f, 10.0f);
            ButtonRoundnessActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Button Roundiness", &selected_button->roundness, 0.0f, 10.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
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
                    selected_button->color.r / 255.0f,
                    selected_button->color.g / 255.0f,
                    selected_button->color.b / 255.0f,
                    selected_button->color.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Default Button Color", (float*)&defaultButtonColorImGui, ImGuiColorEditFlags_NoInputs);

                Color defaultButtonColor = {
                    (unsigned char)(defaultButtonColorImGui.x * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.y * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.z * 255.0f),
                    (unsigned char)(defaultButtonColorImGui.w * 255.0f)
                };

                selected_button->color = defaultButtonColor;
            }
            
            // Button Hover Color
            {
                ImGui::Text("Hover Color: ");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);

                ImVec4 buttonHoverColorImGui = ImVec4(
                    selected_button->hoverColor.r / 255.0f,
                    selected_button->hoverColor.g / 255.0f,
                    selected_button->hoverColor.b / 255.0f,
                    selected_button->hoverColor.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Button Hover Color", (float*)&buttonHoverColorImGui, ImGuiColorEditFlags_NoInputs);

                Color buttonHoverColor = {
                    (unsigned char)(buttonHoverColorImGui.x * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.y * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.z * 255.0f),
                    (unsigned char)(buttonHoverColorImGui.w * 255.0f)
                };

                selected_button->hoverColor = buttonHoverColor;

            }

            // Button Pressed Color
            {
                ImGui::Text("Pressed Color: ");
                ImGui::SameLine(labelWidth);
                ImGui::SetNextItemWidth(-1);

                ImVec4 buttonPressedColorImGui = ImVec4(
                    selected_button->pressedColor.r / 255.0f,
                    selected_button->pressedColor.g / 255.0f,
                    selected_button->pressedColor.b / 255.0f,
                    selected_button->pressedColor.a / 255.0f
                );


                bool colorChanged = ImGui::ColorEdit4("##Button Pressed Color", (float*)&buttonPressedColorImGui, ImGuiColorEditFlags_NoInputs);

                Color buttonPressedColor = {
                    (unsigned char)(buttonPressedColorImGui.x * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.y * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.z * 255.0f),
                    (unsigned char)(buttonPressedColorImGui.w * 255.0f)
                };

                selected_button->pressedColor = buttonPressedColor;

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

        size_t buffer_size = selected_button->text.text.length() + 100;
        char text_buffer[buffer_size];

        strncpy(text_buffer, selected_button->text.text.c_str(), buffer_size - 1);
        text_buffer[buffer_size - 1] = '\0';

        if (ImGui::InputTextMultiline("##Text", text_buffer, IM_ARRAYSIZE(text_buffer), ImVec2(-1, 115)))
        {
            selected_button->text.text = text_buffer;
        }

        ImGui::Text("Font Size:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!FontSizeActiveInputMode) {
            ImGui::SliderFloat("##Font Size", &selected_button->text.fontSize, 0.0f, 100.0f);
            FontSizeActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Font Size", &selected_button->text.fontSize, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                FontSizeActiveInputMode = false;
        }


        ImGui::Text("Spacing:");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        if (!TextSpacingActiveInputMode) {
            ImGui::SliderFloat("##Text Spacing", &selected_button->text.spacing, 0.0f, 100.0f);
            TextSpacingActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("##Text Spacing", &selected_button->text.spacing, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                TextSpacingActiveInputMode = false;
        }




        ImGui::Text("Color: ");
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(-1);

        ImVec4 text_colorImGui = ImVec4(
            selected_button->text.color.r / 255.0f,
            selected_button->text.color.g / 255.0f,
            selected_button->text.color.b / 255.0f,
            selected_button->text.color.a / 255.0f
        );

        
        bool colorChanged = ImGui::ColorEdit4("##Text Color", (float*)&text_colorImGui, ImGuiColorEditFlags_NoInputs);

        Color text_color = {
            (unsigned char)(text_colorImGui.x * 255.0f),
            (unsigned char)(text_colorImGui.y * 255.0f),
            (unsigned char)(text_colorImGui.z * 255.0f),
            (unsigned char)(text_colorImGui.w * 255.0f)
        };

        if (colorChanged) {
            selected_button->text.color = text_color;
        }




        selected_button->text.backgroundColor.a = 0;

    }
}
