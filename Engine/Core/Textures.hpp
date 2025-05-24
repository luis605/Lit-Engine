/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef TEXTURES_HPP
#define TEXTURES_HPP

#include <raylib.h>
#include <string>
#include <pthread.h>
#include <vector>
#include <memory>
#include <glad.h>

struct GBuffer {
    unsigned int id;        // OpenGL framebuffer object id
    Texture color;          // Color  buffer attachment texture
    Texture normal;         // Normal buffer attachment texture
    Texture depth;          // Depth  buffer attachment texture

    void BindGBufferTexturesForRead();
};

GBuffer LoadMRT(const int width, const int height);
void UnloadMRT(const GBuffer& gBuffer);
void BeginMRTMode(const GBuffer& gBuffer);
void EndMRTMode();

struct AsyncTextureData {
    bool textureCreated = false;
    bool compressed     = false;
    bool loaded         = false;
    bool error          = false;
    std::vector<uint8_t> compressedData;
    GLint compressedFormat = GL_RGBA;
    std::string errorMessage;
    std::string filePath;
    pthread_mutex_t mutex;
    Image image;
};

struct AsyncTexture {
    std::shared_ptr<AsyncTextureData> data;
    Texture2D texture;
    pthread_t thread;
};

namespace AsyncTextureManager {
    extern std::vector<std::shared_ptr<AsyncTexture>> asyncTextures;

    void* LoadImageThread(void* arg);
    std::shared_ptr<AsyncTexture> LoadTextureAsync(const std::string& filePath);
    void* LoadImageThread(void* arg);
    void ProcessPendingUpdates();
    void UnloadAsyncTexture(std::shared_ptr<AsyncTexture> asyncTex);
    void UnloadAll();
} // namespace AsyncTextureManager

#endif // TEXTURES_HPP