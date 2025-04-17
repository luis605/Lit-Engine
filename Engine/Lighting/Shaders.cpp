#include <Engine/Lighting/Shaders.hpp>
#include <raylib.h>
#include <rlgl.h>
#include <glad.h>
#include <vector>

GLuint exposureShaderProgram;
GLuint lightsBuffer;
GLuint renderPrevierLightsBuffer;
GLuint exposureSSBO;

float bloomThreshold = 0.2f;
float bloomIntensity = 0.5f;
float prevExposure   = 1.0f;
int kernelSize       = 1;
bool bloomEnabled    = false;

RenderTexture verticalBlurTexture;
RenderTexture horizontalBlurTexture;
RenderTexture upsamplerTexture;
std::vector<RenderTexture2D> downsampledTextures;

void ShaderManager::InitShaders() {
    m_defaultShader         = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    m_instancingShader      = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/lighting_fragment.glsl");
    m_horizontalBlurShader  = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/blurHorizontal.fs");
    m_verticalBlurShader    = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/blurVertical.fs");
    m_upsamplerShader       = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/upsampler.glsl");
    m_downsampleShader      = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl", "Engine/Lighting/shaders/downsampler.glsl");

    char* shaderCode = LoadFileText("Engine/Lighting/shaders/luminanceCompute.glsl");
    unsigned int shaderData = rlCompileShader(shaderCode, RL_COMPUTE_SHADER);
    m_exposureShaderProgram = rlLoadComputeShaderProgram(shaderData);
    UnloadFileText(shaderCode);
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

    auto shader = std::make_shared<Shader>(std::move(loaded));
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

    GLint location = glGetUniformLocation(shaderId, name);
    shaderCache[name] = location;
    return location;
}

const GLint ShaderManager::GetAttribLocation(const GLuint& shaderId, const char* name) noexcept {
    if (!name || name[0] == '\0') return -1;

    auto& shaderCache = attribLocationCache[shaderId];

    if (auto it = shaderCache.find(name); it != shaderCache.end())
        return it->second;

    GLint location = glGetAttribLocation(shaderId, name);
    shaderCache[name] = location;
    return location;
}

ShaderManager shaderManager;