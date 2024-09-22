#ifndef SKYBOX_H
#define SKYBOX_H

void InitSkybox(
    const char* skyboxFileName = "assets/images/skybox/default skybox.hdr",
    Shader skyboxShader = LoadShaderFromMemory(skyboxVert, skyboxFrag),
    Shader cubemapShader = LoadShaderFromMemory(cubemapVert, cubemapFrag)
);


#endif // SKYBOX_H