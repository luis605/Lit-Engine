#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <extras/IconsFontAwesome6.h>

Vector3 selectedEntityScale = {1, 1, 1};
Vector3 selectedEntityPosition = {0, 0, 0};

bool showAlbedoTexture = false;
bool showNormalTexture = false;
bool showRoughnessTexture = false;
bool showAOTexture = false;
bool showHeightTexture = false;
bool showMetallicTexture = false;
bool showEmissiveTexture = false;
bool showSkyboxTexture = false;

bool EntityRotationXInputModel = false;
bool EntityRotationYInputModel = false;
bool EntityRotationZInputModel = false;

bool ButtonRoundnessActiveInputMode = false;

bool AttenuationActiveInputMode = false;
bool IntensityActiveInputMode = false;
bool SpecularStrenghtActiveInputMode = false;
bool RadiusActiveInputMode = false;
bool InnerCutoffActiveInputMode = false;
bool OuterCutoffActiveInputMode = false;

bool FontSizeActiveInputMode = false;
bool TextSpacingActiveInputMode = false;
bool TextBackgroundRoundinessActiveInputMode = false;
bool TextPaddingActiveInputMode = false;

bool WorldGravityXInputMode = false;
bool WorldGravityYInputMode = false;
bool WorldGravityZInputMode = false;

void ShowTexture() {
    if (showAlbedoTexture) {
        ImGui::Begin("Diffuse Texture", &showAlbedoTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0]
                         .maps[MATERIAL_MAP_DIFFUSE]
                         .texture,
                     ImVec2(350, 350));
        ImGui::End();
    }

    if (showNormalTexture) {
        ImGui::Begin("Normal Texture", &showNormalTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0]
                         .maps[MATERIAL_MAP_NORMAL]
                         .texture,
                     ImVec2(350, 350));
        ImGui::End();
    }

    if (showRoughnessTexture) {
        ImGui::Begin("Roughness Texture", &showRoughnessTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0]
                         .maps[MATERIAL_MAP_ROUGHNESS]
                         .texture,
                     ImVec2(350, 350));
        ImGui::End();
    }

    if (showAOTexture) {
        ImGui::Begin("Ambient Occlusion Texture", &showAOTexture);
        ImGui::Image((ImTextureID)&selectedEntity->model.materials[0]
                         .maps[MATERIAL_MAP_OCCLUSION]
                         .texture,
                     ImVec2(350, 350));
        ImGui::End();
    }
}

void Inspector() {
    ImGui::Begin((std::string(ICON_FA_CIRCLE_INFO) + " Inspector").c_str(),
                 NULL);

    if (selectedGameObjectType == "entity" && selectedEntity) {
        if (selectedEntity->getFlag(Entity::Flag::INITIALIZED))
            EntityInspector();
    } else if (selectedGameObjectType == "light") {
        LightInspector();
    } else if (selectedGameObjectType == "text") {
        TextInspector();
    } else if (selectedGameObjectType == "button") {
        ButtonInspector();
    } else if (selectedGameObjectType == "material") {
        ImGui::Text("Inspecting Material");
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        MaterialInspector();
    } else if (selectedGameObjectType == "camera")
        CameraInspector();
    else {
        WorldInspector();
    }

    ImGui::End();

    ShowTexture();
}