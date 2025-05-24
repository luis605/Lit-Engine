/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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
#include <memory>

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
        TraceLog(LOG_WARNING, "Media %s does not exist.", filePath);
    }
}

bool SurfaceMaterialTexture::hasTexture() const {
    return staticTexture && IsTextureReady(staticTexture->texture); // Left to right evaluation -> Memory Safe
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