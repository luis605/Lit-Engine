#include "raylib.h"
#include <glad.h>
#include <iostream>

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
