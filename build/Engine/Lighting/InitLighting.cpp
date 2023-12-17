#include "../../include_all.h"



void SetupShadowMapping()
{

}


void InitLighting()
{
    // Vector4 ambientLight = {0.2f, 0.2f, 0.2f, 1.0f};
    SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

    GLuint shaderProgramID = shader.id;  // Obtain the shader program ID from the Shader object

    glGenBuffers(1, &lightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(), lights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer); // Update the binding point to 3

    // Set the lightsCount uniform
    int lightsCount = lights.size();
    glUniform1i(GetShaderLocation(shader, "lightsCount"), lightsCount);
}