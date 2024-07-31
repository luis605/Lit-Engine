void SetupShadowMapping() {
}


void InitLighting() {
    SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);

    glGenBuffers(1, &lightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, lightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * lights.size(), lights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, lightsBuffer);

    glGenBuffers(1, &renderPrevierLightsBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, renderPrevierLightsBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(Light) * renderModelPreviewerLights.size(), renderModelPreviewerLights.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, renderPrevierLightsBuffer);

    int lightsCount = lights.size();
    glUniform1i(GetShaderLocation(shader, "lightsCount"), lightsCount);
}