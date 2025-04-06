#include "raylib.h"
#include <glad.h>
#include <iostream>
#include <Engine/Core/Textures.hpp>
#include <vector>
#include <algorithm>

void CompressAndStoreTexture(Texture2D* texture) {
    if (texture == nullptr) {
        TraceLog(LOG_WARNING, "Invalid texture pointer.");
        return;
    }

    Image image = LoadImageFromTexture(*texture);
    if (image.width == 0 || image.height == 0) {
        TraceLog(LOG_WARNING, "Failed to load image from texture when trying to compress it.");
        return;
    }

    // Ensure the image is in 32-bit RGBA format
    if (image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
        ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    }

    int width = image.width;
    int height = image.height;

    // Generate a new OpenGL texture
    GLuint compressedTextureID;
    glGenTextures(1, &compressedTextureID);
    glBindTexture(GL_TEXTURE_2D, compressedTextureID);

    GLint internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; // Default

    if (GLAD_GL_KHR_texture_compression_astc_hdr) {
        internalFormat = GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
    } else if (GLAD_GL_EXT_texture_compression_s3tc) {
        if (false) { //isNormalMap) {
            internalFormat = GL_COMPRESSED_RG_RGTC2;  // **Use BC5 for normal maps**
        } else {
            internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;  // **Use BC7 for diffuse**
        }
    } else {
        TraceLog(LOG_WARNING, "No optimal compression available. Using uncompressed format.");
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
        UnloadImage(image);
        return;
    }

    // Upload compressed texture
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data);

    // Generate Mipmap and Set Texture Parameters
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Validate Compression
    GLint compressedSize = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &compressedSize);
    if (compressedSize == 0) {
        TraceLog(LOG_WARNING, "Compression may have failed.");
    } else {
        TraceLog(LOG_INFO, "Compressed texture size: %d bytes", compressedSize);
    }

    // Unload the original image
    UnloadImage(image);

    // Update the Texture2D struct
    texture->id = compressedTextureID;
    texture->width = width;
    texture->height = height;
    texture->mipmaps = 1;
    texture->format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
}

void* AsyncTextureManager::LoadImageThread(void* arg) {
    AsyncTextureData* data = reinterpret_cast<AsyncTextureData*>(arg);
    Image img = LoadImage(data->filePath.c_str());
    // Added error detection for failed image loading.
    if (img.width == 0 || img.height == 0) {
        pthread_mutex_lock(&data->mutex);
        // Assuming AsyncTextureData now has members 'error' (bool) and 'errorMessage' (std::string)
        data->error = true;
        data->errorMessage = "Failed to load image from file.";
        data->loaded = true;  // Mark as loaded to allow processing.
        pthread_mutex_unlock(&data->mutex);
        return nullptr;
    }
    pthread_mutex_lock(&data->mutex);
    data->image = img;
    data->loaded = true;
    pthread_mutex_unlock(&data->mutex);
    return nullptr;
}

AsyncTexture* AsyncTextureManager::LoadTextureAsync(const std::string& filePath) {
    // Allocate and initialize a new async texture.
    AsyncTexture* asyncTex = new AsyncTexture;

    // Create a dummy placeholder texture.
    Image dummyImage = GenImageColor(128, 128, PURPLE);
    asyncTex->texture = LoadTextureFromImage(dummyImage);
    UnloadImage(dummyImage);

    // Allocate and initialize async data.
    asyncTex->data = new AsyncTextureData();
    asyncTex->data->filePath = filePath;
    asyncTex->data->loaded = false;
    asyncTex->data->textureCreated = false;
    pthread_mutex_init(&asyncTex->data->mutex, nullptr);

    // Start the IO thread that loads the image from disk.
    if (pthread_create(&asyncTex->thread, nullptr, LoadImageThread, asyncTex->data) != 0) {
        // Thread creation failed; clean up and return the dummy texture.
        pthread_mutex_destroy(&asyncTex->data->mutex);
        delete asyncTex->data;
        asyncTex->data = nullptr;
    }

    // Add the new async texture to the global container.
    asyncTextures.push_back(asyncTex);
    return asyncTex;
}

void AsyncTextureManager::ProcessPendingUpdates() {
    for (AsyncTexture* asyncTex : asyncTextures) {
        if (asyncTex->data == nullptr) continue;
        pthread_mutex_lock(&asyncTex->data->mutex);
        // Check for error flag in addition to a completed load.
        bool readyToCreate = asyncTex->data->loaded && !asyncTex->data->textureCreated;
        bool errorOccurred = asyncTex->data->error;
        pthread_mutex_unlock(&asyncTex->data->mutex);

        if (errorOccurred) {
            TraceLog(LOG_WARNING, "Error loading texture from file: %s; %s",
                asyncTex->data->filePath.c_str(), asyncTex->data->errorMessage.c_str());
            pthread_mutex_lock(&asyncTex->data->mutex);
            asyncTex->data->textureCreated = true;
            pthread_mutex_unlock(&asyncTex->data->mutex);
            continue;
        }

        if (readyToCreate) {
            pthread_mutex_lock(&asyncTex->data->mutex);
            Texture2D newTexture = LoadTextureFromImage(asyncTex->data->image);
            // CompressAndStoreTexture(&newTexture);
            asyncTex->data->textureCreated = true;
            UnloadImage(asyncTex->data->image);
            pthread_mutex_unlock(&asyncTex->data->mutex);
            UnloadTexture(asyncTex->texture);
            asyncTex->texture = newTexture;
        }
    }
}

void AsyncTextureManager::UnloadAsyncTexture(AsyncTexture* asyncTex) {
    if (asyncTex == nullptr) return;

    pthread_join(asyncTex->thread, nullptr);

    pthread_mutex_lock(&asyncTex->data->mutex);
    if (asyncTex->data->loaded && !asyncTex->data->textureCreated) {
        UnloadImage(asyncTex->data->image);
    }
    pthread_mutex_unlock(&asyncTex->data->mutex);
    pthread_mutex_destroy(&asyncTex->data->mutex);

    delete asyncTex->data;
    asyncTex->data = nullptr;

    UnloadTexture(asyncTex->texture);

    auto it = std::find(asyncTextures.begin(), asyncTextures.end(), asyncTex);
    if (it != asyncTextures.end()) {
        asyncTextures.erase(it);
    }

    delete asyncTex;
}

void AsyncTextureManager::UnloadAll() {
    // Copy vector to avoid issues during iteration.
    std::vector<AsyncTexture*> texturesToUnload = AsyncTextureManager::asyncTextures;
    for (AsyncTexture* asyncTex : texturesToUnload) {
        UnloadAsyncTexture(asyncTex);
    }
    AsyncTextureManager::asyncTextures.clear();
}