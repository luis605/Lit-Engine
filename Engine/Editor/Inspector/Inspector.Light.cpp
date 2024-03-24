#include "../../include_all.h"

void LightInspector()
{
    ImGui::Text("Inspecting Light");

    if (std::holds_alternative<Light*>(objectInInspector)) {
        selectedLight = std::get<Light*>(objectInInspector);
    }

    ImGui::Dummy(ImVec2(0.0f, 15.0f));


    if (ImGui::CollapsingHeader("Light Direction")) {
        ImGui::Indent(30.0f);
        ImGui::SliderFloat("X", &selectedLight->direction.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Y", &selectedLight->direction.y, -1.0f, 1.0f);
        ImGui::SliderFloat("Z", &selectedLight->direction.z, -1.0f, 1.0f);
        ImGui::Unindent(30.0f);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader("Light Properties")) {
        ImGui::Indent(30.0f);



        ImVec4 light_colorImGui = ImVec4(
            selectedLight->color.r,
            selectedLight->color.g,
            selectedLight->color.b,
            selectedLight->color.a
        );

        ImGui::Text("Color: ");
        ImGui::ColorEdit4("##Change_Light_Color", (float*)&light_colorImGui, ImGuiColorEditFlags_NoInputs);
        glm::vec4 light_color = {
            (float)(light_colorImGui.x),
            (float)(light_colorImGui.y),
            (float)(light_colorImGui.z),
            (float)(light_colorImGui.w)
        };

        selectedLight->color = light_color;

        static const char* lights_types[] = { "Directional Light", "Point Light", "Spot Light" };
        static int currentItem = selectedLight->type;

        if (ImGui::BeginCombo("Light Types", lights_types[currentItem])) {
            for (int index = 0; index < IM_ARRAYSIZE(lights_types); index++) {
                const bool isSelected = (currentItem == index);
                if (ImGui::Selectable(lights_types[index], isSelected))
                    currentItem = index;
                    selectedLight->type = currentItem;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }


        ImGui::Dummy(ImVec2(0.0f, 15.0f));



        if (!AttenuationActiveInputMode) {
            ImGui::SliderFloat("Attenuation", &selectedLight->attenuation, 0.0f, 1.0f);
            AttenuationActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Attenuation", &selectedLight->attenuation, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                AttenuationActiveInputMode = false;
        }

        if (!IntensityActiveInputMode) {
            ImGui::SliderFloat("Intensity", &selectedLight->intensity, 0.0f, 5.0f);
            IntensityActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Intensity", &selectedLight->intensity, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                IntensityActiveInputMode = false;
        }

        if (!SpecularStrenghtActiveInputMode) {
            ImGui::SliderFloat("Specular Strenght", &selectedLight->specularStrength, 0.0f, 1.0f);
            SpecularStrenghtActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Specular Strenght", &selectedLight->specularStrength, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                SpecularStrenghtActiveInputMode = false;
        }

        if (!CutoffActiveInputMode) {
            ImGui::SliderFloat("Range", &selectedLight->cutOff, 0.0f, 10000.0f);
            CutoffActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Range", &selectedLight->cutOff, 0.0f, 10000.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                CutoffActiveInputMode = false;
        }

        ImGui::Unindent(30.0f);
    }
}
