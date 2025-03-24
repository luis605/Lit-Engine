#ifndef SURFACE_MAT_HPP
#define SURFACE_MAT_HPP

#include <Engine/GUI/Video/video.hpp>
#include <filesystem>
#include <raylib.h>

namespace fs = std::filesystem;

struct SurfaceMaterialTexture {
    int activatedMode = -1;
    Texture2D staticTexture = {0};
    Video videoTexture;

    SurfaceMaterialTexture() = default;
    SurfaceMaterialTexture(fs::path& filePath) { loadFromFile(filePath); }

    ~SurfaceMaterialTexture() {
        /*
          cleanup();
          Commented: Would be called when not needed.
          Happened when adding a new entity
        */
    }

    SurfaceMaterialTexture& operator=(const SurfaceMaterialTexture& other) {
        if (this != &other) {
            // cleanup(); // Commented: Would be called when not needed.
            // Requires review. Happened when running game (assign operator) and
            // when adding a new entity

            staticTexture = other.staticTexture;
            videoTexture = other.videoTexture;
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