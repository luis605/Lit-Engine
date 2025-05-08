/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef SURFACE_MAT_HPP
#define SURFACE_MAT_HPP

#include <Engine/GUI/Video/video.hpp>
#include <Engine/Core/Textures.hpp>
#include <filesystem>
#include <raylib.h>
#include <memory>

namespace fs = std::filesystem;

struct SurfaceMaterialTexture {
    int activatedMode        = -1;
    std::shared_ptr<AsyncTexture> staticTexture;
    Video videoTexture;

    SurfaceMaterialTexture() = default;
    SurfaceMaterialTexture(fs::path& filePath) { loadFromFile(filePath); }

    ~SurfaceMaterialTexture() { }

    SurfaceMaterialTexture& operator=(const SurfaceMaterialTexture& other) {
        if (this != &other) {
            staticTexture = other.staticTexture;
            videoTexture  = other.videoTexture;
            activatedMode = other.activatedMode;
        }

        return *this;
    }

    SurfaceMaterialTexture& operator=(const fs::path& filePath) {
        loadFromFile(filePath);

        return *this;
    }

    void loadFromFile(const fs::path& filePath);
    bool hasTexture() const;
    const bool hasVideo() const;
    const bool isEmpty() const;
    const Texture2D& getTexture2D() const;
    Video& getVideo();
    void updateVideo();
    Texture2D& getVideoTexture();
    void cleanup();
};

struct SurfaceMaterial {
    fs::path albedoTexturePath;
    fs::path normalTexturePath;
    fs::path roughnessTexturePath;
    fs::path aoTexturePath;
    fs::path heightTexturePath;
    fs::path metallicTexturePath;
    fs::path emissiveTexturePath;

    SurfaceMaterialTexture albedoTexture;
    SurfaceMaterialTexture normalTexture;
    SurfaceMaterialTexture roughnessTexture;
    SurfaceMaterialTexture aoTexture;
    SurfaceMaterialTexture heightTexture;
    SurfaceMaterialTexture metallicTexture;
    SurfaceMaterialTexture emissiveTexture;
};

#endif // SURFACE_MAT_HPP