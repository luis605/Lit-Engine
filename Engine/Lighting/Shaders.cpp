/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Lighting/Shaders.hpp>
#include <raylib.h>
#include <rlgl.h>
#include <glad.h>
#include <vector>
#include <algorithm>

GLuint exposureShaderProgram;
GLuint lightsBuffer;
GLuint renderPrevierLightsBuffer;
GLuint exposureSSBO;

float bloomThreshold = 0.2f;
float bloomIntensity = 0.5f;
float prevExposure   = 1.0f;
int kernelSize       = 1;
bool bloomEnabled    = false;

bool vignetteEnabled   = false;
float vignetteStrength = 0.5f;
float vignetteRadius   = 0.5f;
Vector4 vignetteColor = { 0, 0, 0, 1 };

bool aberrationEnabled   = false;
Vector3 aberrationOffset = { 0.009, 0.006, -0.006 };

bool filmGrainEnabled = false;
float filmGrainStrength = 0.25f;
float filmGrainSize = 1.0f;
float filmGrainTime = 0.0f;

RenderTexture verticalBlurTexture;
RenderTexture horizontalBlurTexture;
RenderTexture brightPassTexture;
RenderTexture bloomCompositorTexture;
RenderTexture vignetteTexture;
RenderTexture chromaticAberrationTexture;
std::vector<RenderTexture2D> downsampledTextures;

void ShaderManager::InitShaders() {
    m_defaultShader = std::make_shared<Shader>(
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl")
    );

    m_instancingShader = std::make_shared<Shader>(
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl")
    );

    m_horizontalBlurShader  = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/blurHorizontal.fs");
    m_verticalBlurShader    = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/blurVertical.fs");
    m_bloomCompositorShader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/bloomCompositor.glsl");
    m_downsampleShader      = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/downsampler.glsl");
    m_vignetteShader        = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/vignette.fs");
    m_irradianceShader      = LoadShader("Engine/Lighting/shaders/cubemap.vs",           "Engine/Lighting/shaders/irradiance.fs");
    m_chromaticAberration   = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/chromaticAberration.fs");
    m_filmGrainShader       = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/filmGrain.fs");
    m_brightFilterShader    = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/brightFilter.fs");

    char* shaderCode = LoadFileText("Engine/Lighting/shaders/luminanceCompute.glsl");
    unsigned int shaderData = rlCompileShader(shaderCode, RL_COMPUTE_SHADER);
    m_exposureShaderProgram = rlLoadComputeShaderProgram(shaderData);
    UnloadFileText(shaderCode);

    const float strength = 0.5f;
    const float radius = 0.75f;
    const Vector3 color = { 0,0,0 };

    SetShaderValue(shaderManager.m_vignetteShader, shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "strength"), &strength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderManager.m_vignetteShader, shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "radius"),   &radius,   SHADER_UNIFORM_FLOAT);
    SetShaderValue(shaderManager.m_vignetteShader, shaderManager.GetUniformLocation(shaderManager.m_vignetteShader.id, "color"),    &color,    SHADER_UNIFORM_VEC3);

    SetShaderValue(m_filmGrainShader, GetUniformLocation(m_filmGrainShader.id, "grainStrength"), &filmGrainStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(m_filmGrainShader, GetUniformLocation(m_filmGrainShader.id, "grainSize"), &filmGrainSize, SHADER_UNIFORM_FLOAT);
    SetShaderValue(m_filmGrainShader, GetUniformLocation(m_filmGrainShader.id, "time"), &filmGrainTime, SHADER_UNIFORM_FLOAT);
}

std::shared_ptr<Shader> ShaderManager::LoadShaderProgram(const fs::path& vertexShaderPath, const fs::path& fragmentShaderPath) {
    Shader loaded = LoadShader(vertexShaderPath.string().c_str(), fragmentShaderPath.string().c_str());

    if (!IsShaderReady(loaded)) {
        TraceLog(LOG_ERROR, "Failed to load shader program.");
        return nullptr;
    }

    auto shader = std::make_shared<Shader>(std::move(loaded));
    m_shaders.emplace_back(shader);
    return m_shaders.back();
}

std::shared_ptr<Shader> ShaderManager::LoadShaderProgramFromMemory(const char* vertexShader, const char* fragmentShader) {
    Shader loaded = LoadShaderFromMemory(vertexShader, fragmentShader);

    if (!IsShaderReady(loaded)) {
        TraceLog(LOG_ERROR, "Failed to load shader program from memory.");
        return nullptr;
    }

    auto shader = std::make_shared<Shader>(loaded);
    m_shaders.emplace_back(shader);

    return m_shaders.back();
}

void ShaderManager::UnloadShader(const std::shared_ptr<Shader>& shader) noexcept {
    std::erase_if(m_shaders, [&](const std::shared_ptr<Shader>& s) noexcept {
        return s == shader;
    });
}

void ShaderManager::UnloadShader(GLuint shaderID) noexcept {
    std::erase_if(m_shaders, [&](const std::shared_ptr<Shader>& s) noexcept {
        return s && s.get()->id == shaderID;
    });
}

const GLint ShaderManager::GetUniformLocation(const GLuint& shaderId, const char* name) noexcept {
    if (!name || name[0] == '\0') return -1;

    auto& shaderCache = uniformLocationCache[shaderId];

    if (auto it = shaderCache.find(name); it != shaderCache.end())
        return it->second;

    GLint location = rlGetLocationUniform(shaderId, name);
    shaderCache[name] = location;
    return location;
}

const GLint ShaderManager::GetAttribLocation(const GLuint& shaderId, const char* name) noexcept {
    if (!name || name[0] == '\0') return -1;

    auto& shaderCache = attribLocationCache[shaderId];

    if (auto it = shaderCache.find(name); it != shaderCache.end())
        return it->second;

    GLint location = rlGetLocationAttrib(shaderId, name);
    shaderCache[name] = location;
    return location;
}

ShaderManager shaderManager;