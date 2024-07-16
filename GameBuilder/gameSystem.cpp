#define GAME_SHIPPING

#include "../Engine/include_all.h"

void InitWindow();
void WindowMainloop();
void Run();
void StartGame();

#define WindowHeight GetScreenHeight()
#define WindowWidth GetScreenWidth()

std::string gameTitle = "Game";
bool first_time = true;
LitCamera inGameCamera = { 0 };

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

    shader = LoadShader("shaders/lighting_vertex.glsl", "shaders/lighting_fragment.glsl");

    // Face Culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    InitSkybox("assets/default skybox.hdr", LoadShader("shaders/skybox.vs", "shaders/skybox.fs"), LoadShader("shaders/cubemap.vs", "shaders/cubemap.fs"));
    Py_Initialize();
    InitLighting();
}

void WindowMainloop() {
    while (!WindowShouldClose()) {
        Run();
    }
}

void UpdateShader() {
    float cameraPos[3] = { sceneCamera.position.x, sceneCamera.position.y, sceneCamera.position.z };
    SetShaderValue(shader, shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void Run() {
    physics.Update(GetFrameTime());

    BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(inGameCamera);

            DrawSkybox();

            UpdateLightsBuffer(false, lights);
            UpdateInGameGlobals();
            UpdateShader();

            if (first_time) {
                for (Entity& entity : entitiesList) {
                    entity.running_first_time = true;

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