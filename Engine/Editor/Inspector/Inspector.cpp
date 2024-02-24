#include "../../include_all.h"
#include "Inspector.h"




void ShowTexture()
{
    if (showTexture)
    {
        ImGui::Begin("Diffuse Texture", &showTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (showNormalTexture)
    {
        ImGui::Begin("Normal Texture", &showNormalTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (showRoughnessTexture)
    {
        ImGui::Begin("Roughness Texture", &showRoughnessTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture, ImVec2(350, 350));
        ImGui::End();
    }

    if (showAOTexture)
    {
        ImGui::Begin("Ambient Occlusion Texture", &showAOTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture, ImVec2(350, 350));
        ImGui::End();
    }
}

void Inspector()
{
    ImGui::Begin((std::string(ICON_FA_CIRCLE_INFO) + " Inspector").c_str(), NULL);
    
    if (selectedGameObjectType == "entity" && selectedEntity)
    {
        if (selectedEntity->initialized)
            EntityInspector();
    }
    else if (selectedGameObjectType == "light")
        LightInspector();
    else if (selectedGameObjectType == "text")
        TextInspector();
    else if (selectedGameObjectType == "button")
        ButtonInspector();
    else if (selectedGameObjectType == "material")
    {
        ImGui::Text("Inspecting Material");    
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        MaterialInspector();
    }
    else if (selectedGameObjectType == "camera")
        CameraInspector();
    else
    {
        WorldInspector();
    }

    ImGui::End();

    ShowTexture();
}


