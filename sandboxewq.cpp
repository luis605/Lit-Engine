#include "raylib.h"
#include "rlgl.h"
#include "dependencies/include/glad/glad.h"
#include <iostream>

// Vertex shader
const char* vsCode = R"(
    #version 330 core
    layout(location = 0) in vec3 inPosition;
    out vec3 fragPosition;
    void main() {
        gl_Position = vec4(inPosition, 1.0);
        fragPosition = inPosition;
    }
)";

// Tessellation Control Shader
const char* tcsCode = R"(
    #version 330 core
    layout(vertices = 3) out;
    in vec3 fragPosition[];
    out vec3 tcPosition[];
    uniform float tessLevel = 4.0;
    void main() {
        gl_TessLevelOuter[0] = tessLevel;
        gl_TessLevelOuter[1] = tessLevel;
        gl_TessLevelOuter[2] = tessLevel;
        gl_TessLevelInner[0] = tessLevel;
        tcPosition[gl_InvocationID] = fragPosition[gl_InvocationID];
    }
)";

// Tessellation Evaluation Shader
const char* tesCode = R"(
    #version 330 core
    layout(triangles, equal_spacing, cw) in;
    in vec3 tcPosition[];
    out vec3 fragPosition;
    uniform float tessLevel = 4.0;
    void main() {
        vec3 p0 = gl_in[0].gl_Position.xyz;
        vec3 p1 = gl_in[1].gl_Position.xyz;
        vec3 p2 = gl_in[2].gl_Position.xyz;
        vec3 pos = gl_TessCoord.x * p0 + gl_TessCoord.y * p1 + gl_TessCoord.z * p2;
        fragPosition = pos;
        gl_Position = vec4(pos, 1.0);
    }
)";

// Fragment shader
// Fragment shader
const char* fsCode = R"(
    #version 330 core
    in vec3 fragPosition;
    out vec4 fragColor;

    void main() {
        // Calculate a color based on the fragment's position
        vec3 normalizedPosition = normalize(fragPosition);
        fragColor = vec4(1.0);
    }
)";


int main() {
    SetTraceLogLevel(LOG_WARNING);
    const int screenWidth = 800;
    const int screenHeight = 600;

    InitWindow(screenWidth, screenHeight, "Tessellation Shader Example");

    // Initialize Raylib and OpenGL
    SetConfigFlags(FLAG_MSAA_4X_HINT); // Enable 4x anti-aliasing for better graphics quality
    InitWindow(screenWidth, screenHeight, "Tessellation Shader Example");

    // Compile and link the shaders
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsCode, NULL);
    glCompileShader(vs);

    unsigned int tcs = glCreateShader(GL_TESS_CONTROL_SHADER);
    glShaderSource(tcs, 1, &tcsCode, NULL);
    glCompileShader(tcs);

    unsigned int tes = glCreateShader(GL_TESS_EVALUATION_SHADER);
    glShaderSource(tes, 1, &tesCode, NULL);
    glCompileShader(tes);

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsCode, NULL);
    glCompileShader(fs);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, tcs);
    glAttachShader(program, tes);
    glAttachShader(program, fs);
    glLinkProgram(program);

    // Check for shader compilation and linking errors here
    glGetError();

    glUseProgram(program);

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 10.0f, 10.0f, 10.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;


    glUseProgram(program);
    GLint tessLevelLocation = glGetUniformLocation(program, "tessLevel");
    glUniform1f(tessLevelLocation, 20000000.0f);


    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        UpdateCamera(&camera, CAMERA_FREE);

        // Draw a tessellated triangle
        rlPushMatrix();
        rlBegin(RL_TRIANGLES);


        // Generate a tessellated sphere
        Mesh sphere = GenMeshSphere(10, 16, 16);

        // Compile and link the shaders
        unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsCode, NULL);
        glCompileShader(vs);

        unsigned int tcs = glCreateShader(GL_TESS_CONTROL_SHADER);
        glShaderSource(tcs, 1, &tcsCode, NULL);
        glCompileShader(tcs);

        unsigned int tes = glCreateShader(GL_TESS_EVALUATION_SHADER);
        glShaderSource(tes, 1, &tesCode, NULL);
        glCompileShader(tes);

        unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsCode, NULL);
        glCompileShader(fs);

        unsigned int program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, tcs);
        glAttachShader(program, tes);
        glAttachShader(program, fs);
        glLinkProgram(program);

        // Check for shader compilation and linking errors here

        glUseProgram(program);

        // Set the tessellation level
        glUniform1f(tessLevelLocation, 432312.0f);

        // Render the tessellated sphere
        glBindVertexArray(sphere.vaoId);
        glDrawElements(GL_TRIANGLES, sphere.indices, GL_UNSIGNED_INT, 0);


        rlEnd();
        rlPopMatrix();

        EndMode3D();
        EndDrawing();
    }

    CloseWindow();

    return 0;
}