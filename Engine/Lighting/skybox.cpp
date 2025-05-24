/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include "skybox.hpp"
#include <Engine/Core/Engine.hpp>
#include <filesystem>
#include <glad.h>
#include <string>
#include <vector>

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

namespace fs = std::filesystem;

Skybox skybox;

void RenderCube() {
    static GLuint cubeVAO = 0, cubeVBO = 0;
    if (cubeVAO == 0) {
        GLfloat vertices[] = {
            -1.0f, -1.0f, -1.0f,  0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
            1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
            1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,
            -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
            1.0f, -1.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, -1.0f, 1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            -1.0f, -1.0f, -1.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 1.0f , 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
            -1.0f, 1.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f
        };


        const size_t stride = 8 * sizeof(GLfloat);
        const GLsizei   count  = sizeof(vertices) / stride;

        glGenVertexArrays(1, &cubeVAO);
        glGenBuffers(1, &cubeVBO);

        glBindVertexArray(cubeVAO);
          glBindBuffer(GL_ARRAY_BUFFER, cubeVBO);
          glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

          // pos
          glEnableVertexAttribArray(0);
          glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
          // normal
          glEnableVertexAttribArray(1);
          glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(GLfloat)));
          // uv
          glEnableVertexAttribArray(2);
          glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(GLfloat)));
        glBindVertexArray(0);
    }

    // draw
    glBindVertexArray(cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void Skybox::loadSkybox(const fs::path& texturePath) {
    color = {1, 1, 1, 1};

    cubeModel = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    skyboxShader = LoadShader("Engine/Lighting/shaders/skybox.vs",
                              "Engine/Lighting/shaders/skybox.fs");
    cubemapShader = LoadShader("Engine/Lighting/shaders/cubemap.vs",
                               "Engine/Lighting/shaders/cubemap.fs");

    skyboxTexturePath = texturePath;
    cubeModel.materials[0].shader = skyboxShader;

    int envMapLoc = shaderManager.GetUniformLocation(skyboxShader.id, "environmentMap");
    int gammaLoc  = shaderManager.GetUniformLocation(skyboxShader.id, "doGamma");
    int vflipLoc  = shaderManager.GetUniformLocation(skyboxShader.id, "vflipped");
    int cubeMapEnvMap = shaderManager.GetUniformLocation(cubemapShader.id, "equirectangularMap");
    int irrEnvMapLoc = shaderManager.GetUniformLocation(shaderManager.m_irradianceShader.id, "environmentMap");

    constexpr int skyboxCubeMap = MATERIAL_MAP_CUBEMAP;
    constexpr int ZERO  = 0;
    constexpr int ONE   = 1;

    SetShaderValue(skyboxShader, envMapLoc,      &skyboxCubeMap, SHADER_UNIFORM_INT);
    SetShaderValue(skyboxShader, gammaLoc,       &ONE,           SHADER_UNIFORM_INT);
    SetShaderValue(skyboxShader, vflipLoc,       &ONE,           SHADER_UNIFORM_INT);
    SetShaderValue(cubemapShader, cubeMapEnvMap, &ZERO,          SHADER_UNIFORM_INT);
    SetShaderValue(shaderManager.m_irradianceShader, irrEnvMapLoc, &ZERO, SHADER_UNIFORM_INT);

    // Load HDR equirectangular texture and generate cubemap
    cubeMap = LoadTexture(texturePath.string().c_str());
    cubeModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture = GenTextureCubemap(cubemapShader, cubeMap, 500, cubeMap.format);

    // Generate irradiance map
    irradianceTex.id = -1;
    GenerateIrradianceMap(cubeModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture.id, shaderManager.m_irradianceShader, irradianceTex.id);

    if (irradianceTex.id == -1) TraceLog(LOG_ERROR, "Failed to generate irradiance texture.");

    irradianceTex.width = irradianceTex.height = 64;
    irradianceTex.format = cubeModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture.format;
    irradianceTex.mipmaps = 1;
}

void Skybox::GenerateIrradianceMap(GLuint envCubemap, Shader& irradianceShader, GLuint& outIrradianceMap) {
    constexpr unsigned int IRR_RES = 64;

    rlDisableBackfaceCulling();

    glGenTextures(1, &outIrradianceMap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outIrradianceMap);

    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                     0, GL_RGB16F, IRR_RES, IRR_RES, 0, GL_RGB, GL_FLOAT, nullptr);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo, rbo;
    glGenFramebuffers(1, &fbo);
    glGenRenderbuffers(1, &rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, IRR_RES, IRR_RES);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        TraceLog(LOG_WARNING, "Irradiance FBO incomplete!");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    Matrix captureProj = MatrixPerspective(90.0 * DEG2RAD, 1.0, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);

    Matrix captureViews[6] = {
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},  {0.0f, -1.0f, 0.0f}),
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}),
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {0.0f, 0.0f, 1.0f}),
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}),
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, -1.0f, 0.0f}),
        MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f})
    };


    glViewport(0, 0, IRR_RES, IRR_RES);
    glUseProgram(irradianceShader.id);

    const int envLoc = shaderManager.GetUniformLocation(irradianceShader.id, "environmentMap");
    const int projLoc = shaderManager.GetUniformLocation(irradianceShader.id, "matProjection");

    glUniform1i(envLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCubemap);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    rlSetUniformMatrix(irradianceShader.locs[SHADER_LOC_MATRIX_PROJECTION], captureProj);

    for (unsigned int i = 0; i < 6; ++i) {
        rlSetUniformMatrix(irradianceShader.locs[SHADER_LOC_MATRIX_VIEW], captureViews[i]);

        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
            outIrradianceMap, 0
        );
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        RenderCube();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);

    rlEnableBackfaceCulling();
}

void Skybox::addSkyboxObject(const Texture2D& texture, const Vector2& scale,
                             const Vector2& velocity, const Vector2& rotation) {
    skyboxObjects.emplace_back(std::move(texture), std::move(scale),
                               std::move(velocity), std::move(rotation));
}

void Skybox::addSkyboxObject(const SkyboxObject& object) {
    skyboxObjects.emplace_back(std::move(object));
}

void Skybox::updateBuffer() {
    int objectsCount = static_cast<int>(skyboxObjects.size());

    for (int index = 0; index < objectsCount; ++index) {
        auto& object = skyboxObjects[index];
        object.rotation.x += 10 * GetFrameTime();
        object.rotation.y += 10 * GetFrameTime();

        std::string textureUniformName =
            "objectTextures[" + std::to_string(index) + "]";

        SetShaderValueTexture(
            skyboxShader,
            shaderManager.GetUniformLocation(skyboxShader.id, textureUniformName.c_str()),
            object.texture);

        SetShaderValue(
            skyboxShader,
            shaderManager.GetUniformLocation(skyboxShader.id,
                               TextFormat("objects[%i].enabled", index)),
            &object.enabled, SHADER_UNIFORM_INT);
        SetShaderValue(
            skyboxShader,
            shaderManager.GetUniformLocation(skyboxShader.id,
                               TextFormat("objects[%i].scale", index)),
            &object.scale, SHADER_UNIFORM_VEC2);
        SetShaderValue(
            skyboxShader,
            shaderManager.GetUniformLocation(skyboxShader.id,
                               TextFormat("objects[%i].rotation", index)),
            &object.rotation, SHADER_UNIFORM_VEC2);
    }

    SetShaderValue(skyboxShader,
                   shaderManager.GetUniformLocation(skyboxShader.id, "objectsCount"),
                   &objectsCount, SHADER_UNIFORM_INT);
}

void Skybox::drawSkybox(LitCamera& camera) {
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    rlDisableDepthTest();
    updateBuffer();
    DrawModel(cubeModel, {0, 0, 0}, 1.0f, WHITE);
    rlEnableDepthTest();
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Skybox::setExposure(float skyboxExposure) {
    SetShaderValue(this->skyboxShader,
                   shaderManager.GetUniformLocation(skyboxShader.id, "skyboxExposure"),
                   &skyboxExposure, SHADER_UNIFORM_FLOAT);
}

TextureCubemap Skybox::GenTextureCubemap(Shader shader, Texture2D panorama,
                                         int size, int format) {
    TextureCubemap cubemap = {0};

    rlDisableBackfaceCulling();

    unsigned int rbo = rlLoadTextureDepth(size, size, true);
    cubemap.id = rlLoadTextureCubemap(0, size, format);

    unsigned int fbo = rlLoadFramebuffer();
    rlFramebufferAttach(fbo, rbo, RL_ATTACHMENT_DEPTH,
                        RL_ATTACHMENT_RENDERBUFFER, 0);
    rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                        RL_ATTACHMENT_CUBEMAP_POSITIVE_X, 0);

    if (rlFramebufferComplete(fbo))
        TraceLog(LOG_INFO,
                 "FBO: [ID %i] Framebuffer object created successfully", fbo);

    rlEnableShader(shader.id);

    Matrix matFboProjection = MatrixPerspective(
        90.0 * DEG2RAD, 1.0, RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_PROJECTION],
                       matFboProjection);

    Matrix fboViews[6] = {MatrixLookAt({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                                       {0.0f, -1.0f, 0.0f}),
                          MatrixLookAt({0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
                                       {0.0f, -1.0f, 0.0f}),
                          MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
                                       {0.0f, 0.0f, 1.0f}),
                          MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                       {0.0f, 0.0f, -1.0f}),
                          MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                                       {0.0f, -1.0f, 0.0f}),
                          MatrixLookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f},
                                       {0.0f, -1.0f, 0.0f})};

    rlViewport(0, 0, size, size);

    rlActiveTextureSlot(0);
    rlEnableTexture(panorama.id);

    for (int i = 0; i < 6; i++) {
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_VIEW], fboViews[i]);

        rlFramebufferAttach(fbo, cubemap.id, RL_ATTACHMENT_COLOR_CHANNEL0,
                            RL_ATTACHMENT_CUBEMAP_POSITIVE_X + i, 0);
        rlEnableFramebuffer(fbo);

        rlClearScreenBuffers();
        rlLoadDrawCube();
    }

    rlDisableShader();
    rlDisableTexture();
    rlDisableFramebuffer();
    rlUnloadFramebuffer(fbo);

    rlViewport(0, 0, rlGetFramebufferWidth(), rlGetFramebufferHeight());
    rlEnableBackfaceCulling();

    cubemap.width = size;
    cubemap.height = size;
    cubemap.mipmaps = 1;
    cubemap.format = format;

    return cubemap;
}