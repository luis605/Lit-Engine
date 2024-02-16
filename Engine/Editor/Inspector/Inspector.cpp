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
        MaterialInspector();
    else if (selected_game_object_type == "camera")
        CameraInspector();
    else
    {
        WorldInspector();
    }

    ImGui::End();

    ShowTexture();
}


