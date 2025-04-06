#include <Engine/Core/Engine.hpp>
#include <Engine/Lighting/InitLighting.hpp>
#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/lights.hpp>
#include <raylib.h>
#include <glad.h>

void InitLighting() {
    SetShaderValue(shader, GetUniformLocation(shader.id, "ambientLight"),
                   &ambientLight, SHADER_UNIFORM_VEC4);

    glGenBuffers(1, &lightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(),
                 lights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer);

    glGenBuffers(1, &renderPrevierLightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderPrevierLightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 sizeof(Light) * renderModelPreviewerLights.size(),
                 renderModelPreviewerLights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, renderPrevierLightsBuffer);

    int lightsCount = lights.size();
    glUniform1i(GetUniformLocation(shader.id, "lightsCount"), lightsCount);

    float initialExposure = 1.0f;
    glGenBuffers(1, &exposureSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, exposureSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float), &initialExposure,
                 GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, exposureSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}