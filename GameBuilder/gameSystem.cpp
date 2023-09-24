#define GAME_SHIPPING

#include "../include_all.h"

void InitWindow();
void WindowMainloop();
void Run();
void StartGame();

#define WindowHeight GetScreenHeight() / 1.5
#define WindowWidth GetScreenWidth() / 1.5

string gameTitle = "My Own Game!";
bool first_time = true;


Camera3D inGame_Camera = { 0 };




#include <iostream>
#include <fstream>
#include <string>

const char* encryptFile(const std::string& inputFile, const std::string& key) {
    std::ifstream inFile(inputFile, std::ios::binary);

    if (!inFile) {
        std::cerr << "Failed to open files." << std::endl;
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



void InitWindow()
{
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(WindowWidth, WindowHeight, gameTitle.c_str());
    
    string key_a = "99ec8e6c8f47650000bc98440c36a7898a80f1a18bbf395d92fd8564774a11cdee77b83fedfcf324731f449bd00b29cf854199991e6a92cd23cc63ab8b4adf5d800ee7d4239a36fa4e2e546ea5f61dec41d750bff7be78e2ace47774b4eaadc826b83fceb5316aea4937c604bc8eb25c2cc410414a9eea583c6610da0d3b87e03c49915f2f609d398ff1a8631211eb51f0a324fa7f30af8cc6ab4f4b14329aba2fba34f9e7b930c203351c4c9537159dc8cfbda40b902a01ec27b1817b60284f42b0313c8f835c244d3cfc98b45f0eb149c2d1d027911a47ff66256b0bfab84ddf0d7e2bb146e8d874e6aa589697d8f69e5034d7171dd0c5d199fabb500743615924c03e2f7ed74717db7483937dcf2b5a211fa5e4bf57e3f4e53909999aab792b66c350db1156a840f633d322096c06b22d4ed5fea413fee182ae05559a17ccfcca4a14a8bc7ea77fc67abefe7f05e14e13069f7622d0bd33e16dd35b999dac93abcb690b6d2261ba79f3bdc18ef77e8ef2a9f22973b4bed1da8277fcf5a71a27bb9b7fded476161f3d67b5d12c5d1c993c833f119d23553adc04eb2d1452792208f2a2ff4d61ab0929fa76fbe4";
    const char* frag = decryptFile("whatami.glsl", key_a);

    string key_b = "141b5aceaaa5582ec3efb9a17cac2da5e52bbc1057f776e99a56a064f5ea40d5f8689b7542c4d0e9d6d7163b9dee7725369742a54905ac95c74be5cb1435fdb726fead2437675eaa13bc77ced8fb9cc6108d4a247a2b37b76a6e0bf41916fcc98ee5f85db11ecb52b0d94b5fbab58b1f4814ed49e761a7fb9dfb0960f00ecf8c87989b8e92a630680128688fa7606994e3be12734868716f9df27674700a2cb37440afe131e570a4ee9e7e867aab18a44ee972956b7bd728f9b937c973b9726f6bdd56090d720e6fa31c70b31e0216739cde4210bcd93671c1e8edb752b32f782b62eab4d77a51e228a6b6ac185d7639bd037f9195c3f05c5d2198947621814827f2d99dd7c2821e76635a845203f42060e5a9a494482afab1c42c23ba5f317f250321c7713c2ce19fe7a3957ce439f4782dbee3d418aebe08314a4d6ac7b3d987696d39600c5777f555a8dc99f2953ab45b0687efa1a77d8e5b448b37a137f2849c9b76fec98765523869c22a3453c214ec8e8827acdded27c37d96017fbf862a405b4b06fe0e815e09ed5288ccd9139e67c7feed3e7306f621976b9d3ba917d19ef4a13490f9e2af925996f59a87uihjoklas9emyuikw75igeturf7u";
    const char* vert = decryptFile("whereami.glsl", key_b);
    shader = LoadShaderFromMemory(vert, frag);

    InitLighting();
    SetTargetFPS(60);

    inGame_Camera.position = (Vector3){ 0.0f, 10.0f, 10.0f };  
    inGame_Camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };      
    inGame_Camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };          
    inGame_Camera.fovy = 45.0f;                                
    inGame_Camera.projection = CAMERA_PERSPECTIVE;             

    SetupPhysicsWorld();

    // DisableCursor();

    InitSkybox(
        "assets/default skybox.hdr",
        "shaders/skybox.vs",
        "shaders/skybox.fs",
        "shaders/cubemap.vs",
        "shaders/cubemap.fs"
    );

}

void WindowMainloop()
{
    while (!WindowShouldClose())
    {
        Run();
    }
}



void Run()
{
    dynamicsWorld->stepSimulation(GetFrameTime(), 10);

    BeginDrawing();
        ClearBackground(GRAY);

        BeginMode3D(inGame_Camera);

            DrawSkybox();
            
            UpdateInGameGlobals();

            for (Entity& entity : entities_list)
            {
                RenderAndRunEntity(entity, scripts_thread_vector, first_time, &inGame_Camera);
            }


            if (first_time)
            {
                for (auto& script_thread : scripts_thread_vector) {
                    if (script_thread.joinable())
                        script_thread.detach();
                }
            }

            first_time = false;

        EndMode3D();

    DrawTextElements();
    DrawButtons();
    
    EndDrawing();
}


int main()
{
    InitWindow();
    LoadProject(entities_list, lights, lights_info);
    WindowMainloop();
    CloseWindow();
}