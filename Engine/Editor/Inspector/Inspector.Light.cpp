#include "../../include_all.h"

void LightInspector()
{
    ImGui::Text("Inspecting Light");

    if (std::holds_alternative<Light*>(object_in_inspector)) {
        selected_light = std::get<Light*>(object_in_inspector);
    }

    ImVec4 light_colorImGui = ImVec4(
        selected_light->color.r,
        selected_light->color.g,
        selected_light->color.b,
        selected_light->color.a
    );

    ImGui::Text("Color: ");
    ImGui::ColorEdit4("##Change_Light_Color", (float*)&light_colorImGui, ImGuiColorEditFlags_NoInputs);
    glm::vec4 light_color = {
        (float)(light_colorImGui.x),
        (float)(light_colorImGui.y),
        (float)(light_colorImGui.z),
        (float)(light_colorImGui.w)
    };

    selected_light->color = light_color;

    static const char* lights_types[] = { "Directional Light", "Point Light", "Spot Light" };
    static int currentItem = selected_light->type;

    if (ImGui::BeginCombo("Light Types", lights_types[currentItem])) {
        for (int index = 0; index < IM_ARRAYSIZE(lights_types); index++) {
            const bool isSelected = (currentItem == index);
            if (ImGui::Selectable(lights_types[index], isSelected))
                currentItem = index;
                selected_light->type = currentItem;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }


    if (ImGui::CollapsingHeader("Light Direction")) {
        ImGui::Indent(30.0f);
        ImGui::SliderFloat("X", &selected_light->direction.x, -1.0f, 1.0f);
        ImGui::SliderFloat("Y", &selected_light->direction.y, -1.0f, 1.0f);
        ImGui::SliderFloat("Z", &selected_light->direction.z, -1.0f, 1.0f);
        ImGui::Unindent(30.0f);
    }




    ImGui::Dummy(ImVec2(0.0f, 5.0f));





    if (ImGui::CollapsingHeader("Light Properties")) {
        ImGui::Indent(30.0f);

        if (!AttenuationActiveInputMode) {
            ImGui::SliderFloat("Attenuation", &selected_light->attenuation, 0.0f, 1.0f);
            AttenuationActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Attenuation", &selected_light->attenuation, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                AttenuationActiveInputMode = false;
        }

        if (!IntensityActiveInputMode) {
            ImGui::SliderFloat("Intensity", &selected_light->intensity, 0.0f, 100.0f);
            IntensityActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Intensity", &selected_light->intensity, 0.0f, 100.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                IntensityActiveInputMode = false;
        }

        if (!SpecularStrenghtActiveInputMode) {
            ImGui::SliderFloat("Specular Strenght", &selected_light->specularStrength, 0.0f, 1.0f);
            SpecularStrenghtActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Specular Strenght", &selected_light->specularStrength, 0.0f, 1.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                SpecularStrenghtActiveInputMode = false;
        }

        if (!CutoffActiveInputMode) {
            ImGui::SliderFloat("Range", &selected_light->cutOff, 0.0f, 10000.0f);
            CutoffActiveInputMode = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        else
        {
            if (ImGui::InputFloat("Range", &selected_light->cutOff, 0.0f, 10000.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                CutoffActiveInputMode = false;
        }

        ImGui::Unindent(30.0f);
    }
}
