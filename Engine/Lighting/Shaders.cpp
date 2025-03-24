#include <Engine/Lighting/Shaders.hpp>
#include <raylib.h>
#include <glad.h>
#include <vector>

Shader shader;
Shader instancingShader;
Shader horizontalBlurShader;
Shader verticalBlurShader;
Shader downsampleShader;
Shader upsamplerShader;
GLuint exposureShaderProgram;
GLuint lightsBuffer;
GLuint renderPrevierLightsBuffer;
GLuint exposureSSBO;

bool bloomEnabled = false;
float bloomThreshold = 0.2f;
float bloomIntensity = 0.5f;
float prevExposure = 1.0f;
int kernelSize = 1;

RenderTexture verticalBlurTexture;
RenderTexture horizontalBlurTexture;
RenderTexture upsamplerTexture;
std::vector<RenderTexture2D> downsampledTextures;