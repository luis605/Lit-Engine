#ifndef TEXTURES_HPP
#define TEXTURES_HPP

#include <raylib.h>
#include <string>
#include <pthread.h>
#include <vector>

void CompressAndStoreTexture(Texture2D* texture);


struct AsyncTextureData {
    std::string filePath;         // File path of the texture to load.
    Image image;                  // CPU-side image loaded from disk.
    std::string errorMessage;     // Error message if loading fails.
    bool error;                   // Flag indicating an error occurred.
    bool loaded;                  // Flag indicating the image is loaded.
    bool textureCreated;          // Flag indicating the GPU texture was created.
    pthread_mutex_t mutex;        // Mutex for thread-safe access.
};

struct AsyncTexture {
    Texture2D texture;       // The texture used for rendering (dummy until real texture is loaded).
    AsyncTextureData* data;  // Pointer to async load data.
    pthread_t thread;        // Thread handle for the IO thread.
};

namespace AsyncTextureManager {
    // Global container of all active async textures.
    static std::vector<AsyncTexture*> asyncTextures;

    // Thread function that loads the image from disk into system memory.
    void* LoadImageThread(void* arg);

    // Load a texture asynchronously.
    // Returns an AsyncTexture handle that immediately contains a dummy texture.
    AsyncTexture* LoadTextureAsync(const std::string& filePath);

    // Processes all pending async texture updates.
    // Should be called once per frame on the main thread.
    void ProcessPendingUpdates();

    // Free the resources associated with an AsyncTexture.
    void UnloadAsyncTexture(AsyncTexture* asyncTex);

    void UnloadAll();

    } // namespace AsyncTextureManager
#endif // TEXTURES_HPP