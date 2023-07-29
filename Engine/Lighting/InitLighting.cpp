#include "../../include_all.h"



void SetupShadowMapping()
{

}


void InitLighting()
{
    nonOpaqueUniforms.colDiffuse = {1.0f, 1.0f, 1.0f, 1.0f}; // Example data, replace with your own values
    nonOpaqueUniforms.ambientLight = {0.2f, 0.2f, 0.2f, 1.0f}; // Example data, replace with your own values
    nonOpaqueUniforms.viewPos = {0.0f, 0.0f, 0.0f}; // Example data, replace with your own values
    nonOpaqueUniforms.lightsCount = 3; // Example data, replace with your own values

    unsigned int ubo;
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(NonOpaqueUniforms), &nonOpaqueUniforms, GL_STATIC_DRAW);

    int nonOpaqueUniformsBindingPoint = 1; // Must match the binding point specified in the shader
    glEnableVertexAttribArray(nonOpaqueUniformsBindingPoint);
    glVertexAttribBinding(nonOpaqueUniformsBindingPoint, 1); // Binding index should be unique
    glVertexAttribFormat(nonOpaqueUniformsBindingPoint, 4, GL_FLOAT, GL_FALSE, 0);

    Vector4 ambientLight = {0.2f, 0.2f, 0.2f, 1.0f};
    SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

    GLuint shaderProgramID = shader.id;  // Obtain the shader program ID from the Shader object

    glGenBuffers(1, &lightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(), lights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer); // Update the binding point to 3

    // Set the lightsCount uniform
    int lightsCount = lights.size();
    glUniform1i(GetShaderLocation(shader, "lightsCount"), lightsCount);

    // Setup shadow mapping
    SetupShadowMapping();
}