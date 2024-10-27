void SetupShadowMapping() {
}


void InitLighting() {
    SetShaderValue(shader, GetUniformLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

    glGenBuffers(1, &lightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(), lights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer);

    glGenBuffers(1, &renderPrevierLightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderPrevierLightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * renderModelPreviewerLights.size(), renderModelPreviewerLights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, renderPrevierLightsBuffer);

    int lightsCount = lights.size();
    glUniform1i(GetUniformLocation(shader, "lightsCount"), lightsCount);

    glGenBuffers(1, &downsamplerPixelCountBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, downsamplerPixelCountBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 0, downsamplerPixelCountBuffer); // Binding point 0, same as in the shader

    glGenBuffers(1, &downsamplerLuminanceBuffer);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, downsamplerLuminanceBuffer);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 1, downsamplerLuminanceBuffer); // Binding point 1, same as in the shader
}