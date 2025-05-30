/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <raylib.h>
#include <rlgl.h>
#include <glad.h>
#include <iostream>
#include <Engine/Core/Textures.hpp>
#include <vector>
#include <algorithm>
#include <memory>
#include <squish.h>

GBuffer LoadMRT(const int width, const int height) {
    GBuffer target = { 0 };

    target.id = rlLoadFramebuffer();

    if (target.id > 0) {
        rlEnableFramebuffer(target.id);

        target.color.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.color.width = width;
        target.color.height = height;
        target.color.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        target.color.mipmaps = 1;

        target.normal.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.normal.width = width;
        target.normal.height = height;
        target.normal.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        target.normal.mipmaps = 1;

        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = -1;
        target.depth.mipmaps = 1;

        rlActiveDrawBuffers(2);

        rlFramebufferAttach(target.id, target.color.id,  RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.normal.id, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id,  RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        if (rlFramebufferComplete(target.id)) TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);

        rlDisableFramebuffer();
    } else TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

    return target;
}

void UnloadMRT(const GBuffer& gBuffer) {
    if (gBuffer.id > 0) {
        if (gBuffer.color.id > 0)  rlUnloadTexture(gBuffer.color.id);
        if (gBuffer.normal.id > 0) rlUnloadTexture(gBuffer.normal.id);
        if (gBuffer.depth.id > 0)  rlUnloadTexture(gBuffer.depth.id);

        rlUnloadFramebuffer(gBuffer.id);
    }
}

void BeginMRTMode(const GBuffer& gBuffer) {
    rlEnableFramebuffer(gBuffer.id);
    rlClearScreenBuffers();
}

void EndMRTMode() {
    rlDisableFramebuffer();
}

void GBuffer::BindGBufferTexturesForRead() {
    rlActiveTextureSlot(0); rlEnableTexture(color.id);
    rlActiveTextureSlot(1); rlEnableTexture(normal.id);
    rlActiveTextureSlot(2); rlEnableTexture(depth.id);
};

GLint g_compressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

void DetermineCompressionFormat() {
    if (GLAD_GL_KHR_texture_compression_astc_hdr) {
        g_compressedFormat = GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
    } else if (GLAD_GL_EXT_texture_compression_s3tc) {
        g_compressedFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    } else {
        g_compressedFormat = GL_RGBA;
    }
}

std::vector<std::shared_ptr<AsyncTexture>> AsyncTextureManager::asyncTextures;

void* AsyncTextureManager::LoadImageThread(void* arg) {
    auto dataPtr = static_cast<std::shared_ptr<AsyncTextureData>*>(arg);
    std::shared_ptr<AsyncTextureData> data = *dataPtr;
    delete dataPtr;

    Image img = LoadImage(data->filePath.c_str());

    bool doCompress = false;
    int imgWidth = 0, imgHeight = 0, compressedSize = 0;

    pthread_mutex_lock(&data->mutex);
    if (img.width == 0 || img.height == 0) {
        data->error = true;
        data->errorMessage = "Failed to load image from file.";
    } else {
        if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) {
            ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        }
        data->image = img;
        data->compressedFormat = g_compressedFormat;

        if (g_compressedFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
            doCompress = true;
            imgWidth = img.width;
            imgHeight = img.height;
            compressedSize = squish::GetStorageRequirements(img.width, img.height, squish::kDxt5);
        } else if (g_compressedFormat == GL_COMPRESSED_RGBA_ASTC_8x8_KHR) {
            doCompress = true;
            imgWidth = img.width;
            imgHeight = img.height;
            compressedSize = (img.width / 8) * (img.height / 8) * 16;
        } else if (g_compressedFormat == GL_RGBA) {
            doCompress = false;
        }
    }

    pthread_mutex_unlock(&data->mutex);

    if (doCompress) {
        std::vector<unsigned char> tempCompressed(compressedSize);
        squish::CompressImage(static_cast<const squish::u8*>(img.data),
                                imgWidth,
                                imgHeight,
                                tempCompressed.data(),
                                squish::kDxt5);

        pthread_mutex_lock(&data->mutex);
        data->compressedData = std::move(tempCompressed);
        data->compressed = true;
        pthread_mutex_unlock(&data->mutex);
    }

    pthread_mutex_lock(&data->mutex);
    data->loaded = true;
    pthread_mutex_unlock(&data->mutex);

    return nullptr;
}

std::shared_ptr<AsyncTexture> AsyncTextureManager::LoadTextureAsync(const std::string& filePath) {
    DetermineCompressionFormat();
    auto asyncTex = std::make_shared<AsyncTexture>();

    Image dummyImage = GenImageColor(128, 128, PURPLE);
    asyncTex->texture = LoadTextureFromImage(dummyImage);
    UnloadImage(dummyImage);

    asyncTex->data = std::make_shared<AsyncTextureData>();
    asyncTex->data->filePath = filePath;
    pthread_mutex_init(&asyncTex->data->mutex, nullptr);

    auto dataCopy = new std::shared_ptr<AsyncTextureData>(asyncTex->data);
    if (pthread_create(&asyncTex->thread, nullptr, LoadImageThread, dataCopy) != 0) {
        pthread_mutex_destroy(&asyncTex->data->mutex);
        return asyncTex;
    }

    asyncTextures.emplace_back(asyncTex);
    return asyncTex;
}

void AsyncTextureManager::ProcessPendingUpdates() {
    for (auto& asyncTex : asyncTextures) {
        if (!asyncTex || !asyncTex->data) continue;

        pthread_mutex_lock(&asyncTex->data->mutex);
        bool ready = asyncTex->data->loaded && !asyncTex->data->textureCreated;
        bool error = asyncTex->data->error;
        pthread_mutex_unlock(&asyncTex->data->mutex);

        if (error) {
            pthread_mutex_lock(&asyncTex->data->mutex);
            asyncTex->data->textureCreated = true;
            pthread_mutex_unlock(&asyncTex->data->mutex);
            continue;
        }

        if (ready) {
            pthread_mutex_lock(&asyncTex->data->mutex);
            if (asyncTex->data->compressed) {
                GLuint textureID;
                glGenTextures(1, &textureID);
                glBindTexture(GL_TEXTURE_2D, textureID);
                glCompressedTexImage2D(GL_TEXTURE_2D, 0, asyncTex->data->compressedFormat,
                                      asyncTex->data->image.width, asyncTex->data->image.height,
                                      0, asyncTex->data->compressedData.size(), asyncTex->data->compressedData.data());
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                Texture2D newTexture;
                newTexture.id = textureID;
                newTexture.width = asyncTex->data->image.width;
                newTexture.height = asyncTex->data->image.height;
                newTexture.mipmaps = 1;
                newTexture.format = PIXELFORMAT_COMPRESSED_DXT5_RGBA;
                asyncTex->texture = newTexture;
            } else {
                Texture2D newTexture = LoadTextureFromImage(asyncTex->data->image);
                asyncTex->texture = newTexture;
            }
            UnloadImage(asyncTex->data->image);
            asyncTex->data->textureCreated = true;
            pthread_mutex_unlock(&asyncTex->data->mutex);
        }
    }
}

void AsyncTextureManager::UnloadAsyncTexture(std::shared_ptr<AsyncTexture> asyncTex) {
    if (!asyncTex) return;

    pthread_join(asyncTex->thread, nullptr);
    if (asyncTex->data) {
        pthread_mutex_destroy(&asyncTex->data->mutex);
    }
    UnloadTexture(asyncTex->texture);

    auto it = std::find(asyncTextures.begin(), asyncTextures.end(), asyncTex);
    if (it != asyncTextures.end()) asyncTextures.erase(it);
}

void AsyncTextureManager::UnloadAll() {
    for (auto& asyncTex : asyncTextures) UnloadAsyncTexture(asyncTex);
    asyncTextures.clear();
}