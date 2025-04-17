#ifndef TEXTURES_HPP
#define TEXTURES_HPP

#include <raylib.h>
#include <string>
#include <pthread.h>
#include <vector>
#include <memory>
#include <glad.h>

void CompressAndStoreTexture(Texture2D* texture);

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