#define GAME_SHIPPING

#include <Engine/Core/Engine.hpp>
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/RunGame.hpp>
#include <Engine/Core/SaveLoad.hpp>
#include <Engine/Lighting/InitLighting.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/Shaders.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Physics/PhysicsManager.hpp>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Scripting/time.hpp>
#include <cstddef>
#include <fstream>
#include <glad.h>
#include <ios>
#include <iostream>
#include <python3.12/Python.h>
#include <raylib.h>
#include <string>

void InitWindow();
void WindowMainloop();
void Run();
void StartGame();

#define WindowHeight GetScreenHeight()
#define WindowWidth GetScreenWidth()

std::string gameTitle = "Game";
bool first_time = true;
LitCamera inGameCamera = {0};

const char* encryptFile(const std::string& inputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);

    if (!inFile) {
        std::cerr << "Failed to open encrypted shader files." << std::endl;
        return nullptr;
    }

    size_t keyLength = key.size();
    size_t bufferSize = 4096;
    char buffer[4096];

    size_t bytesRead = 0;
    size_t keyIndex = 0;
    std::string encryptedData;

    while (inFile.good()) {
        inFile.read(buffer, bufferSize);
        bytesRead = inFile.gcount();

        for (size_t i = 0; i < bytesRead; ++i) {
            buffer[i] ^= key[keyIndex++];
            keyIndex %= keyLength;
        }

        encryptedData.append(buffer, bytesRead);
    }

    char* encryptedCString = new char[encryptedData.size() + 1];
    std::strcpy(encryptedCString, encryptedData.c_str());

    return encryptedCString;
}

const char* decryptFile(const std::string& inputFile, const std::string& key) {
    return encryptFile(inputFile, key);
}

void InitWindow() {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(WindowWidth, WindowHeight, gameTitle.c_str());

    shader = LoadShader("shaders/lighting_vertex.glsl",
                        "shaders/lighting_fragment.glsl");

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    skybox.loadSkybox("assets/default skybox.hdr");
    Py_Initialize();
    InitLighting();
}

void WindowMainloop() {
    while (!WindowShouldClose()) {
        Run();
    }
}

void UpdateGameShader() {
    float cameraPos[3] = {inGameCamera.position.x, inGameCamera.position.y,
                          inGameCamera.position.z};
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos,
                   SHADER_UNIFORM_VEC3);
}

void Run() {
    physics.Update(GetFrameTime());

    BeginDrawing();
    ClearBackground(GRAY);

    BeginMode3D(inGameCamera);

    skybox.drawSkybox(inGameCamera);

    UpdateLightsBuffer(false, lights);
    UpdateInGameGlobals();
    UpdateGameShader();
    UpdateFrustum();

    if (first_time) {
        for (Entity& entity : entitiesList) {
            entity.setFlag(Entity::Flag::RUNNING_FIRST_TIME, true);

            RenderAndRunEntity(entity, &inGameCamera);
        }
    }

    for (Entity& entity : entitiesList) {
        entity.render();

        entity.runScript(&inGameCamera);
    }

    first_time = false;

    EndMode3D();

    DrawTextElements();
    DrawButtons();

    EndDrawing();
}

int main() {
    InitWindow();
    LoadProject(entitiesList, lights, inGameCamera);
    WindowMainloop();
    CloseWindow();
}