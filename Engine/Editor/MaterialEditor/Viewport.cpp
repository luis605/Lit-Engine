/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include "Viewport.hpp"
#include <fstream>
#include "imgui.h"

ViewportPanel::ViewportPanel() { }
ViewportPanel::~ViewportPanel() { }

void ViewportPanel::Init() {
    this->m_MaterialCamera.position   = { 10.0f, 10.0f, 10.0f };
    this->m_MaterialCamera.target     = { 0.0f,  0.0f,  0.0f };
    this->m_MaterialCamera.up         = { 0.0f,  1.0f,  0.0f };
    this->m_MaterialCamera.fovy       = 60.0f;
    this->m_MaterialCamera.projection = CAMERA_PERSPECTIVE;

    this->m_MaterialRT = LoadRenderTexture(1.0f, 1.0f);

    this->m_Entity.emplace();
    this->m_Entity->initializeDefaultModel();

    LightStruct lightA;
    lightA.light.type     = LIGHT_POINT;
    lightA.light.position = { 11.0f, 6.0f, 15.0f };
    lightA.light.color    = { 1.0f, 0.75f, 0.75f, 1.0f };
    lightA.light.params.y = 10.0f;

    LightStruct lightB;
    lightB.light.type     = LIGHT_POINT;
    lightB.light.position = { 20.0f, 10.0f, -25.0f };
    lightB.light.color    = { 1.0f, 0.16f, 0.16f, 1.0f };
    lightB.light.params.y = 6.0f;

    LightStruct lightC;
    lightC.light.type     = LIGHT_POINT;
    lightC.light.position = { -5.0f, -3.0f, -12.0f };
    lightC.light.color    = { 1.0f, 0.50f, 0.32f, 1.0f };
    lightC.light.params.y = 6.0f;

    this->m_Lights.push_back(lightA);
    this->m_Lights.push_back(lightB);
    this->m_Lights.push_back(lightC);
}

void ViewportPanel::ApplyPreviewFragmentShader(const std::string& fragSource) {
    std::ifstream file("Engine/Lighting/shaders/lighting_vertex.glsl");
    std::stringstream buffer;
    buffer << file.rdbuf();

    Shader shader = LoadShaderFromMemory(buffer.str().c_str(), fragSource.c_str());
    std::shared_ptr<Shader> shaderPtr = std::make_shared<Shader>(shader);

    this->m_Entity->setShader(shaderPtr);

    return;
}

void ViewportPanel::Render() {
    ImGui::Begin("Viewport");

    ImVec2 windowDimensions = ImGui::GetWindowSize();

    if ((!IsRenderTextureValid(this->m_MaterialRT)) ||
        (this->m_MaterialRT.texture.width != windowDimensions.x) ||
        (this->m_MaterialRT.texture.height != windowDimensions.y)
    ) {
        UnloadRenderTexture(this->m_MaterialRT);
        this->m_MaterialRT = LoadRenderTexture(windowDimensions.x, windowDimensions.y);
    }

    BeginTextureMode(this->m_MaterialRT);
    BeginMode3D(this->m_MaterialCamera);
    ClearBackground(GRAY);

        sceneEditor.UpdateShader(this->m_MaterialCamera);
        skybox.drawSkybox(this->m_MaterialCamera);
        UpdateLightsBuffer(true, this->m_Lights);

        float cameraPos[3] = { this->m_MaterialCamera.position.x, this->m_MaterialCamera.position.y, this->m_MaterialCamera.position.z };
        SetShaderValue(*this->m_Entity->getShader(), shaderManager.GetUniformLocation((*this->m_Entity->getShader()).id, "viewPos"), cameraPos, SHADER_UNIFORM_VEC3);

        GLuint program = this->m_Entity->getShader()->id;
        glUseProgram(program);

        GLint loc = glGetUniformLocation(program, "u_env_humidity");
        if (loc >= 0) glUniform1f(loc, m_EnvHumidity);

        this->m_Entity->render(this->m_Lights.size());

    EndMode3D();
    EndTextureMode();

    ImGui::SetCursorPosY(0);
    ImGui::Image((ImTextureID)&this->m_MaterialRT.texture,
                windowDimensions,
                ImVec2(0, 1), ImVec2(1, 0)
    );

    ImGui::SliderFloat("Humidity", &m_EnvHumidity, 0.0f, 1.0f);
    ImGui::SameLine();
    if (ImGui::Button("Simulate Rain")) {
        m_EnvHumidity = 1.0f;
    }

    SetShaderValue(*this->m_Entity->getShader(), GetShaderLocation(*this->m_Entity->getShader(), "u_env_humidity"), &m_EnvHumidity, SHADER_UNIFORM_FLOAT);

    ImGui::End();
}