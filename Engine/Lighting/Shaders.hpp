#ifndef SHADERS_HPP
#define SHADERS_HPP

#include <raylib.h>
#include <glad.h>
#include <vector>

extern Shader shader;
extern Shader instancingShader;
extern Shader horizontalBlurShader;
extern Shader verticalBlurShader;
extern Shader downsampleShader;
extern Shader upsamplerShader;
extern GLuint exposureShaderProgram;
extern GLuint lightsBuffer;
extern GLuint renderPrevierLightsBuffer;
extern GLuint exposureSSBO;

extern bool bloomEnabled;
extern float bloomThreshold;
extern float bloomIntensity;
extern float prevExposure;
extern int kernelSize;

extern RenderTexture verticalBlurTexture;
extern RenderTexture horizontalBlurTexture;
extern RenderTexture upsamplerTexture;
extern std::vector<RenderTexture2D> downsampledTextures;

#endif // SHADERS_HPP