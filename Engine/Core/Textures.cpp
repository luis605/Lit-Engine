#include "raylib.h"
#include <glad.h>
#include <iostream>

void CompressAndStoreTexture(Texture2D* texture) {
    // Extract the image data from the existing texture
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

    // Generate a new OpenGL texture and bind it
    GLuint compressedTextureID;
    glGenTextures(1, &compressedTextureID);
    glBindTexture(GL_TEXTURE_2D, compressedTextureID);

    // Upload uncompressed data and let OpenGL handle compression
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, // Request DXT1 compression
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 image.data);

    // Set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Clean up image data
    UnloadImage(image);

    // Update the Texture2D struct with the new compressed texture
    texture->id = compressedTextureID;
    texture->width = width;
    texture->height = height;
}