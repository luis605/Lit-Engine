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

void Skybox::loadSkybox(const fs::path& texturePath) {
    color = {1, 1, 1, 1};
    cubeModel = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    skyboxShader = LoadShader("Engine/Lighting/shaders/skybox.vs",
                              "Engine/Lighting/shaders/skybox.fs");
    cubemapShader = LoadShader("Engine/Lighting/shaders/cubemap.vs",
                               "Engine/Lighting/shaders/cubemap.fs");

    skyboxTexturePath = texturePath;
    cubeModel.materials[0].shader = skyboxShader;

    int temp_array1[1] = {MATERIAL_MAP_CUBEMAP};
    int useHDR[] = {1};

    SetShaderValue(
        cubeModel.materials[0].shader,
        shaderManager.GetUniformLocation(cubeModel.materials[0].shader.id, "environmentMap"),
        temp_array1, SHADER_UNIFORM_INT);
    SetShaderValue(cubeModel.materials[0].shader,
                   shaderManager.GetUniformLocation(cubeModel.materials[0].shader.id, "doGamma"),
                   useHDR, SHADER_UNIFORM_INT);
    SetShaderValue(
        cubeModel.materials[0].shader,
        shaderManager.GetUniformLocation(cubeModel.materials[0].shader.id, "vflipped"), useHDR,
        SHADER_UNIFORM_INT);

    int array0[1] = {0};
    SetShaderValue(cubemapShader,
                   shaderManager.GetUniformLocation(cubemapShader.id, "equirectangularMap"),
                   array0, SHADER_UNIFORM_INT);

    cubemap = LoadTexture(texturePath.string().c_str());
    cubeModel.materials[0].maps[MATERIAL_MAP_CUBEMAP].texture =
        GenTextureCubemap(cubemapShader, cubemap, 500,
                          PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    glGenBuffers(1, &objectsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, objectsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 sizeof(SkyboxObject) * skyboxObjects.size(),
                 skyboxObjects.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, objectsBuffer);
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