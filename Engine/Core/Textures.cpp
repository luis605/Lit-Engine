/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Textures.hpp"
#include <raylib.h>
#include <rlgl.h>
#include <glad.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <squish.h>

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

GBuffer LoadMRT(const int width, const int height) {
    GBuffer target{ 0 };

    target.id = rlLoadFramebuffer();
    if (target.id > 0) {
        rlEnableFramebuffer(target.id);
        // color
        target.color.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.color.width = width;  target.color.height = height;
        target.color.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; target.color.mipmaps = 1;
        // normal
        target.normal.id = rlLoadTexture(NULL, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
        target.normal.width = width; target.normal.height = height;
        target.normal.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8; target.normal.mipmaps = 1;
        // depth
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;  target.depth.height = height;
        target.depth.format = -1;    target.depth.mipmaps = 1;

        rlActiveDrawBuffers(2);
        rlFramebufferAttach(target.id, target.color.id,  RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.normal.id, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(target.id, target.depth.id,  RL_ATTACHMENT_DEPTH,             RL_ATTACHMENT_TEXTURE2D, 0);

        if (rlFramebufferComplete(target.id))
            TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);
        rlDisableFramebuffer();
    } else {
        TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");
    }

    return target;
}

void UnloadMRT(const GBuffer& gBuffer) {
    if (gBuffer.id > 0) {
        if (gBuffer.color.id  > 0) rlUnloadTexture(gBuffer.color.id);
        if (gBuffer.normal.id > 0) rlUnloadTexture(gBuffer.normal.id);
        if (gBuffer.depth.id  > 0) rlUnloadTexture(gBuffer.depth.id);
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
}

//-----------------------------------------------------------------------------
// Async loader implementation
//-----------------------------------------------------------------------------
std::vector<std::shared_ptr<AsyncTexture>> AsyncTextureManager::asyncTextures;

// Thread function now takes a shared_ptr directly
void AsyncTextureManager::LoadImageThread(std::shared_ptr<AsyncTextureData> data) {
    Image img = LoadImage(data->filePath.c_str());

    bool doCompress = false;
    int imgWidth = 0, imgHeight = 0, compressedSize = 0;

    {
        std::lock_guard<std::mutex> lock(data->mutex);
        if (img.width == 0 || img.height == 0) {
            data->error = true;
            data->errorMessage = "Failed to load image from file.";
        } else {
            if (img.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

            data->image = img;
            data->compressedFormat = g_compressedFormat;

            if (g_compressedFormat == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT) {
                doCompress    = true;
                imgWidth      = img.width;
                imgHeight     = img.height;
                compressedSize = squish::GetStorageRequirements(img.width, img.height, squish::kDxt5);
            } else if (g_compressedFormat == GL_COMPRESSED_RGBA_ASTC_8x8_KHR) {
                doCompress    = true;
                imgWidth      = img.width;
                imgHeight     = img.height;
                compressedSize = (img.width/8)*(img.height/8)*16;
            }
        }
    }

    if (doCompress) {
        std::vector<uint8_t> temp(compressedSize);
        squish::CompressImage(
            reinterpret_cast<const squish::u8*>(img.data),
            imgWidth, imgHeight,
            temp.data(),
            squish::kDxt5
        );

        std::lock_guard<std::mutex> lock(data->mutex);
        data->compressedData = std::move(temp);
        data->compressed     = true;
    }

    {
        std::lock_guard<std::mutex> lock(data->mutex);
        data->loaded = true;
    }
}

std::shared_ptr<AsyncTexture> AsyncTextureManager::LoadTextureAsync(const std::string& filePath) {
    DetermineCompressionFormat();

    auto asyncTex = std::make_shared<AsyncTexture>();
    // generate a placeholder
    Image dummy = GenImageColor(128,128,PURPLE);
    asyncTex->texture = LoadTextureFromImage(dummy);
    UnloadImage(dummy);

    asyncTex->data = std::make_shared<AsyncTextureData>();
    asyncTex->data->filePath = filePath;

    // launch worker thread
    asyncTex->thread = std::thread(&AsyncTextureManager::LoadImageThread, asyncTex->data);

    asyncTextures.push_back(asyncTex);
    return asyncTex;
}

void AsyncTextureManager::ProcessPendingUpdates() {
    for (auto& asyncTex : asyncTextures) {
        auto& data = asyncTex->data;
        if (!data) continue;

        bool ready = false, error = false;
        {
            std::lock_guard<std::mutex> lock(data->mutex);
            ready = (data->loaded && !data->textureCreated);
            error = data->error;
        }

        if (error) {
            std::lock_guard<std::mutex> lock(data->mutex);
            data->textureCreated = true;
            continue;
        }

        if (ready) {
            std::lock_guard<std::mutex> lock(data->mutex);
            if (data->compressed) {
                GLuint texID;
                glGenTextures(1, &texID);
                glBindTexture(GL_TEXTURE_2D, texID);
                glCompressedTexImage2D(
                    GL_TEXTURE_2D, 
                    0, 
                    data->compressedFormat,
                    data->image.width, data->image.height,
                    0, 
                    static_cast<GLsizei>(data->compressedData.size()),
                    data->compressedData.data()
                );
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                Texture2D newTex{ texID, data->image.width, data->image.height, 1, PIXELFORMAT_COMPRESSED_DXT5_RGBA };
                asyncTex->texture = newTex;
            } else {
                asyncTex->texture = LoadTextureFromImage(data->image);
            }
            UnloadImage(data->image);
            data->textureCreated = true;
        }
    }
}

void AsyncTextureManager::UnloadAsyncTexture(std::shared_ptr<AsyncTexture> asyncTex) {
    if (!asyncTex) return;
    if (asyncTex->thread.joinable())
        asyncTex->thread.join();

    if (asyncTex->data) {
        // std::mutex has no destroy; just lets it go out of scope
    }
    UnloadTexture(asyncTex->texture);

    auto it = std::find(asyncTextures.begin(), asyncTextures.end(), asyncTex);
    if (it != asyncTextures.end())
        asyncTextures.erase(it);
}

void AsyncTextureManager::UnloadAll() {
    for (auto& tex : asyncTextures)
        UnloadAsyncTexture(tex);
    asyncTextures.clear();
}