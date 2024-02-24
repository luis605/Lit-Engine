#include "../../include_all.h"
#include "Inspector.h"




void ShowTexture()
{
    if (show_texture)
    {
        ImGui::Begin("Diffuse Texture", &show_texture);
        ImGui::Image((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (show_normal_texture)
    {
        ImGui::Begin("Normal Texture", &show_normal_texture);
        ImGui::Image((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (show_roughness_texture)
    {
        ImGui::Begin("Roughness Texture", &show_roughness_texture);
        ImGui::Image((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (show_ao_texture)
    {
        ImGui::Begin("Ambient Occlusion Texture", &show_ao_texture);
        ImGui::Image((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture, ImVec2(350, 350));
        ImGui::End();
    }
}

void Inspector()
{
    ImGui::Begin((std::string(ICON_FA_CIRCLE_INFO) + " Inspector").c_str(), NULL);
    
    if (selected_game_object_type == "entity" && selected_entity)
    {
        if (selected_entity->initialized)
            EntityInspector();
    }
    else if (selected_game_object_type == "light")
        LightInspector();
    else if (selected_game_object_type == "text")
        TextInspector();
    else if (selected_game_object_type == "button")
        ButtonInspector();
    else if (selected_game_object_type == "material")
    {
        ImGui::Text("Inspecting Material");    
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        MaterialInspector();
    }
    else if (selected_game_object_type == "camera")
        CameraInspector();
    else
    {
        WorldInspector();
    }

    ImGui::End();

    ShowTexture();
}


