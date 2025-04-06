#include "SurfaceMaterial.hpp"
#include "lights.hpp"
#include <Engine/Core/Textures.hpp>
#include <Engine/Core/Filesystem.hpp>
#include <Engine/GUI/Video/video.hpp>
#include <Engine/Lighting/lights.hpp>
#include <filesystem>
#include <glad.h>
#include <iostream>
#include <raylib.h>
#include <string>

namespace fs = std::filesystem;

void SurfaceMaterialTexture::loadFromFile(const fs::path& filePath) {
    if (fs::exists(filePath)) {
        try {
            if (IsImageFile(filePath)) {
                activatedMode = 0;
                staticTexture = AsyncTextureManager::LoadTextureAsync(filePath.string());
            } else if (IsVideoFile(filePath)) {
                activatedMode = 1;
                videoTexture = Video();
                videoTexture.openVideo(filePath.string().c_str());
            }
        } catch (const std::exception& e) {
            TraceLog(LOG_WARNING, "Error loading texture or video file.");
        }
    } else {
        TraceLog(LOG_WARNING, "Texture or video does not exist.");
    }
}

bool SurfaceMaterialTexture::hasTexture() const {
    return IsTextureReady(staticTexture->texture);
}

const bool SurfaceMaterialTexture::hasVideo() const {
    return videoTexture.hasVideoStream();
}

const bool SurfaceMaterialTexture::isEmpty() const {
    return activatedMode == -1;
}

const Texture2D& SurfaceMaterialTexture::getTexture2D() const {
    return staticTexture->texture;
}

Video& SurfaceMaterialTexture::getVideo() { return videoTexture; }

void SurfaceMaterialTexture::updateVideo() { videoTexture.PlayCurrentFrame(); }

Texture2D& SurfaceMaterialTexture::getVideoTexture() {
    return videoTexture.getTexture();
}

void SurfaceMaterialTexture::cleanup() {
    if (hasTexture()) {
        AsyncTextureManager::UnloadAsyncTexture(staticTexture);
        staticTexture = nullptr;
    } else if (hasVideo()) {
        videoTexture.cleanup();
    }
    activatedMode = -1;
}

struct TextureNode {
    SurfaceMaterialTexture texture;
    fs::path texturePath;
};

void UpdateLightsBuffer(bool force, std::vector<LightStruct>& lightsVector,
                        GLuint& buffer) {
    if (lightsVector.empty() && !force)
        return;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);

    size_t bufferSize = sizeof(Light) * lightsVector.size();
    GLsizeiptr currentBufferSize;
    glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE,
                             &currentBufferSize);

    if (bufferSize != static_cast<size_t>(currentBufferSize)) {
        glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, nullptr,
                     GL_DYNAMIC_DRAW);
    }

    std::vector<Light> lightsData;
    lightsData.reserve(lightsVector.size());

    for (const auto& lightStruct : lightsVector) {
        if (lightStruct.lightInfo.enabled)
            lightsData.push_back(lightStruct.light);
    }

    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bufferSize, lightsData.data());

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);

    int lightsCount = static_cast<int>(lightsVector.size());
    SetShaderValue(shader, GetUniformLocation(shader.id, "lightsCount"),
                   &lightsCount, SHADER_UNIFORM_INT);
}