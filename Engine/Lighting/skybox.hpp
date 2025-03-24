#ifndef SKYBOX_H
#define SKYBOX_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <Engine/Scripting/functions.hpp>
#include <filesystem>
#include <glad.h>
#include <raylib.h>
#include <vector>

namespace fs = std::filesystem;

class SkyboxObject {
  public:
    alignas(4) int enabled = 1;
    alignas(8) Vector2 scale;
    alignas(8) Vector2 rotation;
    alignas(8) Vector2 velocity;
    Texture2D texture;
    std::string name;

  public:
    SkyboxObject(const Texture2D& texture, const Vector2& scale,
                 const Vector2& velocity, const Vector2& rotation)
        : texture(texture), scale(scale), velocity(velocity),
          rotation(rotation) {}

    SkyboxObject() {}
};

class Skybox {
  public:
    std::vector<SkyboxObject> skyboxObjects;
    fs::path skyboxTexturePath;
    Vector4 color;
    Model cubeModel;
    Shader skyboxShader;
    Shader cubemapShader;
    TextureCubemap cubemap;
    GLuint objectsBuffer;

  public:
    void loadSkybox(const fs::path& texturePath);
    void addSkyboxObject(const Texture2D& texture, const Vector2& scale,
                         const Vector2& velocity, const Vector2& rotation);
    void addSkyboxObject(const SkyboxObject& object);
    void updateBuffer();
    void drawSkybox(LitCamera& camera);
    void setExposure(float exposure);
    TextureCubemap GenTextureCubemap(Shader shader, Texture2D panorama,
                                     int size, int format);
};

extern Skybox skybox;

#endif // SKYBOX_H