#include "raylib.h"

#include "rlgl.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

#include "dependencies/include/glad/glad.h"

class TessellationShader : public Shader {
public:
    TessellationShader(const char* vsFileName, const char* fsFileName, const char* tcsFileName, const char* tesFileName) {
        Load(vsFileName, fsFileName);
        LoadTessellationControlShader(tcsFileName);
        LoadTessellationEvaluationShader(tesFileName);
        SetShaderCustomUniform(0, rlGetLocationUniform(GetShaderDefault(), "u_tess_level")); // Custom uniform for tessellation level
    }

    void SetTessellationLevel(float level) {
        SetShaderValue(GetShaderDefault(), GetShaderLocation(GetShaderDefault(), "u_tess_level"), &level, UNIFORM_FLOAT, 1);
    }
};



int main()
{
    SetTraceLogLevel(LOG_WARNING);
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;
    InitWindow(screenWidth, screenHeight, "CSM");

    // Setup camera
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Camera3D camera_shadow_map = {0};
    
    SetTargetFPS(60);

    Shader shader = LoadShader("Engine/Lighting/shaders/lod.vs", "Engine/Lighting/shaders/lod.fs");

    int ambientLoc = GetShaderLocation(shader, "ambient");
    SetShaderValue(shader, ambientLoc, &Vector4{ 0.2f, 0.2f, 0.2f, 1.0f }, SHADER_UNIFORM_VEC4);

    Model plane = LoadModelFromMesh(GenMeshPlane(10.0f, 10.0f, 1, 1));
    Model cube = LoadModelFromMesh(GenMeshCube(2.0f, 2.0f, 2.0f));

    plane.materials[0].shader = shader;
    cube.materials[0].shader = shader;

    DisableCursor();

    SetShaderValueTexture(shader, GetShaderLocation(shader, "texture0"), LoadTexture("random.png"));
    // Main game loop
    while (!WindowShouldClose())
    {
        
        SetShaderValue(shader, GetShaderLocation(shader, "viewPos"), &camera.position, SHADER_UNIFORM_VEC3);
        UpdateCamera(&camera, CAMERA_FREE);
        BeginDrawing();
        ClearBackground(RAYWHITE);

        
        BeginMode3D(camera);

        DrawModel(plane, Vector3{0, 0, 0}, 4.0f, WHITE);
        DrawModel(cube, Vector3{0, 3, 0}, 1.0f, RED);

        EndMode3D();

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
