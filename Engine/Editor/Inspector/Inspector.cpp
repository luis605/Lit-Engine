#include "../../include_all.h"
#include "Inspector.h"









void ShowTexture()
{
    if (show_texture)
    {
        ImGui::SetNextWindowSize(ImVec2(entity_texture.width, entity_texture.height));
        ImGui::Begin("Texture Previewer");
        ImGui::Image((ImTextureID)&entity_texture, ImVec2(entity_texture.width, entity_texture.height));
        ImGui::End();
    }

    // if (show_normal_texture)
    // {
    //     ImGui::SetNextWindowSize(ImVec2(selected_entity->normal_texture.width, selected_entity->normal_texture.height));
    //     ImGui::Begin("Normal Texture Previewer");
    //     ImGui::Image((ImTextureID)&selected_entity->normal_texture, ImVec2(selected_entity->normal_texture.width, selected_entity->normal_texture.height));
    //     ImGui::End();
    // }

}


void Inspector()
{
    if (selected_game_object_type == "entity")
        EntityInspector();
    else if (selected_game_object_type == "light")
        LightInspector();
    else if (selected_game_object_type == "text")
        TextInspector();
    else if (selected_game_object_type == "button")
        ButtonInspector();

    ShowTexture();
    MaterialsNodeEditor();
}


