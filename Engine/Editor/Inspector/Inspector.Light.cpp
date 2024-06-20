#include "../../include_all.h"

void LightInspector()
{
    ImGui::Text("Inspecting Light");

    ImGui::Dummy(ImVec2(0.0f, 15.0f));

    if (ImGui::CollapsingHeader(ICON_FA_SLIDERS " Light Properties", false)) {
        ImGui::Indent(30.0f);

        static const char* lights_types[] = { "Directional Light", "Point Light", "Spot Light" };
        static int currentItem = selectedLight->light.type;

        if (ImGui::BeginCombo("Light Types", lights_types[currentItem])) {
            for (int index = 0; index < IM_ARRAYSIZE(lights_types); index++) {
                const bool isSelected = (currentItem == index);
                if (ImGui::Selectable(lights_types[index], isSelected))
                    currentItem = index;
                    selectedLight->light.type = currentItem;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }


        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImGui::Text("Position:");
        ImGui::Indent(20.0f);

        float lightPos[3] = { selectedLight->light.position.x, selectedLight->light.position.y, selectedLight->light.position.z };
        if (ImGui::InputFloat3("##Position", lightPos))
            selectedLight->light.position = glm::vec3(lightPos[0], lightPos[1], lightPos[2]);

        ImGui::Unindent(20.0f);

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImVec4 light_colorImGui = ImVec4(
            selectedLight->light.color.r,
            selectedLight->light.color.g,
            selectedLight->light.color.b,
            selectedLight->light.color.a
        );

        ImGui::Text("Color: ");
        ImGui::ColorEdit4("##Change_Light_Color", (float*)&light_colorImGui, ImGuiColorEditFlags_NoInputs);
        glm::vec4 light_color = {
            (float)(light_colorImGui.x),
            (float)(light_colorImGui.y),
            (float)(light_colorImGui.z),
            (float)(light_colorImGui.w)
        };

        selectedLight->light.color = light_color;

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        if (!AttenuationActiveInputMode) {
            ImGui::SliderFloat("Attenuation", &selectedLight->light.attenuation, 0.0f, 0.3f);
            AttenuationActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Attenuation", &selectedLight->light.attenuation, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                AttenuationActiveInputMode = false;
        }

        if (!IntensityActiveInputMode) {
            ImGui::SliderFloat("Intensity", &selectedLight->light.intensity, 0.0f, 50.0f);
            IntensityActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Intensity", &selectedLight->light.intensity, 0.0f, 100000.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                IntensityActiveInputMode = false;
        }

        if (!SpecularStrenghtActiveInputMode) {
            ImGui::SliderFloat("Specular Strenght", &selectedLight->light.specularStrength, 0.0f, 1.0f);
            SpecularStrenghtActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Specular Strenght", &selectedLight->light.specularStrength, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                SpecularStrenghtActiveInputMode = false;
        }

        if (!CutoffActiveInputMode) {
            ImGui::SliderFloat("Range", &selectedLight->light.cutOff, 0.0f, 50.0f);
            CutoffActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Range", &selectedLight->light.cutOff, 0.0f, 10000.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                CutoffActiveInputMode = false;
        }

        ImGui::Unindent(30.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader(ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT " Light Direction")) {
        ImGui::Indent(30.0f);
        ImGui::SliderFloat("X", &selectedLight->light.direction.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Y", &selectedLight->light.direction.y, -1.0f, 1.0f);
        ImGui::SliderFloat("Z", &selectedLight->light.direction.z, -1.0f, 1.0f);
        ImGui::Unindent(30.0f);
    }
}
