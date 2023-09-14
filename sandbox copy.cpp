#include <raylib.h>
#include "include/raymath.h"
#include "dependencies/include/glm/glm.hpp"
#include "dependencies/include/glm/gtc/matrix_transform.hpp"
#include <vector>
#include <iostream>

// Define the number of cascades you want
constexpr int numCascades = 4;

// Structure to hold light information
struct Light
{
    Vector3 position;
    Color color;
};

std::vector<Vector3> getFrustumCornersWorldSpace(const Matrix& proj, const Matrix& view)
{
    Matrix inv = MatrixInvert(MatrixMultiply(proj, view));

    std::vector<Vector3> frustumCorners;
    for (int x = 0; x < 2; ++x)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int z = 0; z < 2; ++z)
            {
                Vector3 pt =
                {
                    2.0f * x - 1.0f,
                    2.0f * y - 1.0f,
                    2.0f * z - 1.0f
                };
                Vector3 worldPt = Vector3Transform(pt, inv);
                frustumCorners.push_back(worldPt);
            }
        }
    }

    return frustumCorners;
}

const char* depthVertexShaderText =
    "#version 330 core\n"
    "layout(location = 0) in vec3 inPosition;\n"
    "uniform mat4 lightSpaceMatrix;\n"
    "void main()\n"
    "{\n"
    "    gl_Position = lightSpaceMatrix * vec4(inPosition, 1.0);\n"
    "}\n";

// Fragment shader for depth map rendering
const char* depthFragmentShaderText =
    "#version 330 core\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0);\n" // Render depth map as white
    "}\n";


int main()
{
    // Initialize Raylib
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(800, 600, "Cascaded Shadow Mapping Example");

    // Create your camera settings
    Camera camera = { 0 };
    camera.position = (Vector3){ 0, 5, 5 };
    camera.target = (Vector3){ 0, 0, 0 };
    camera.up = (Vector3){ 0, 1, 0 };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Define light properties
    Light light;
    light.position = (Vector3){ -4.0f, 8.0f, -4.0f };
    light.color = WHITE;

    // Create depth map resolution
    int depthMapResolution = 1024;

    // Create light space matrices for each cascade
    Matrix lightSpaceMatrices[numCascades] = { 0 };

    RenderTexture2D depthMap = LoadRenderTexture(depthMapResolution, depthMapResolution);

    // Create cube model
    Model cubeModel = LoadModelFromMesh(GenMeshCube(1, 1, 1));

    // Load the custom depth map shaders
    Shader depthMapShader = LoadShaderFromMemory(depthVertexShaderText, depthFragmentShaderText);

    // Assign the shader to the cube model material
    cubeModel.materials[0].shader = depthMapShader;
    
    // Main loop
    while (!WindowShouldClose())
    {
        // Update camera view and projection matrices
        Matrix viewMatrix = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix projectionMatrix = MatrixPerspective(camera.fovy * DEG2RAD, (float)GetScreenWidth() / GetScreenHeight(), 0.1f, 100.0f);

        std::vector<Vector3> corners = getFrustumCornersWorldSpace(projectionMatrix, viewMatrix);

        // Depth Map Pass
        BeginTextureMode(depthMap);
        BeginShaderMode(depthMapShader);

        for (int cascadeIdx = 0; cascadeIdx < numCascades; ++cascadeIdx)
        {
            // Calculate cascade near and far planes
            float cascadeNear = 0.1f + static_cast<float>(cascadeIdx) * 5.0f;
            float cascadeFar = 5.0f + static_cast<float>(cascadeIdx + 1) * 5.0f;

            // Calculate the light's view matrix
            Matrix lightView = MatrixLookAt(light.position, (Vector3){ 0.0f, 0.0f, 0.0f }, (Vector3){ 0.0f, 1.0f, 0.0f });

            // Calculate the light's projection matrix
            float minX = FLT_MAX;
            float maxX = -FLT_MAX;
            float minY = FLT_MAX;
            float maxY = -FLT_MAX;
            float minZ = FLT_MAX;
            float maxZ = -FLT_MAX;

            for (const auto& v : corners)
            {
                Vector4 trf = {
                    Vector3Transform(Vector3{ v.x, v.y, v.z }, lightView).x,
                    Vector3Transform(Vector3{ v.x, v.y, v.z }, lightView).y,
                    Vector3Transform(Vector3{ v.x, v.y, v.z }, lightView).z,
                    1.0f
                };

                minX = fminf(minX, trf.x);
                maxX = fmaxf(maxX, trf.x);
                minY = fminf(minY, trf.y);
                maxY = fmaxf(maxY, trf.y);
                minZ = fminf(minZ, trf.z);
                maxZ = fmaxf(maxZ, trf.z);
            }

            // Calculate the light's projection matrix
            Matrix lightProjection = MatrixOrtho(minX, maxX, minY, maxY, minZ, maxZ);

            // Store the light space matrix for this cascade
            lightSpaceMatrices[cascadeIdx] = MatrixMultiply(lightProjection, lightView);
        }

        BeginMode3D(camera);


        // Draw the cube model with depth map shader and light space matrices
        for (int cascadeIdx = 0; cascadeIdx < numCascades; ++cascadeIdx)
        {
            // Set depth map shader
            SetShaderValueMatrix(cubeModel.materials[0].shader, GetShaderLocation(cubeModel.materials[0].shader, "lightSpaceMatrix"), lightSpaceMatrices[cascadeIdx]);

            // Draw the cube model
            DrawModel(cubeModel, (Vector3){ 0, 0, 0 }, 1.0f, RED);
        }

        EndShaderMode();
        EndMode3D();

        EndTextureMode();

        // Restore the default render target
        BeginDrawing();
        ClearBackground(RAYWHITE);

        // Draw depth map
        DrawTextureRec(depthMap.texture, (Rectangle){ 0, 0, (float)depthMap.texture.width, -(float)depthMap.texture.height }, (Vector2){ 0, 0 }, WHITE);

        DrawFPS(10, 10);

        EndDrawing();
    }

    // Cleanup Raylib resources
    UnloadRenderTexture(depthMap);
    UnloadModel(cubeModel);
    CloseWindow();

    return 0;
}
