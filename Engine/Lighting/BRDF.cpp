/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <raylib.h>
#include <Engine/Lighting/BRDF.hpp>
#include <utility>

void BRDF::GenLUT() {
    constexpr int size = 512;

    Shader shader = LoadShader(0, "Engine/Lighting/shaders/brdf.fs");
    if (shader.id == 0) {
        TraceLog(LOG_ERROR, "Failed to load BRDF shader.");
        return;
    }

    RenderTexture2D lutRT = LoadRenderTexture(size, size);
    Image tempImage = GenImageColor(size, size, WHITE);
    Texture2D tempTexture = LoadTextureFromImage(tempImage);
    UnloadImage(tempImage);

    BeginTextureMode(lutRT);
        ClearBackground(BLANK);

        BeginShaderMode(shader);
            DrawTexture(tempTexture, 0, 0, WHITE);
        EndShaderMode();
    EndTextureMode();

    UnloadTexture(tempTexture);

    Image lutImage = LoadImageFromTexture(lutRT.texture);
    this->m_brdfLut = LoadTextureFromImage(lutImage);
    UnloadImage(lutImage);

    UnloadRenderTexture(lutRT);
    UnloadShader(shader);
}

BRDF brdf;