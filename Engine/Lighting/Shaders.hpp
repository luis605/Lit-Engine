/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef SHADERS_HPP
#define SHADERS_HPP

#include <raylib.h>
#include <glad.h>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <cstring>

namespace fs = std::filesystem;

extern GLuint lightsBuffer;
extern GLuint renderPrevierLightsBuffer;
extern GLuint exposureSSBO;

extern bool vignetteEnabled;
extern float vignetteStrength;
extern float vignetteRadius;
extern Vector4 vignetteColor;

extern bool bloomEnabled;
extern float bloomThreshold;
extern float bloomIntensity;
extern float prevExposure;
extern int kernelSize;

extern RenderTexture verticalBlurTexture;
extern RenderTexture horizontalBlurTexture;
extern RenderTexture upsamplerTexture;
extern RenderTexture vignetteTexture;
extern std::vector<RenderTexture2D> downsampledTextures;

struct ShaderManager {
private:
    std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> uniformLocationCache;
    std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> attribLocationCache;

public:
    std::vector<std::shared_ptr<Shader>> m_shaders;
    std::shared_ptr<Shader> m_defaultShader;
    std::shared_ptr<Shader> m_instancingShader;
    Shader m_horizontalBlurShader;
    Shader m_verticalBlurShader;
    Shader m_downsampleShader;
    Shader m_upsamplerShader;
    Shader m_vignetteShader;
    Shader m_irradianceShader;
    GLuint m_exposureShaderProgram;

public:
    void InitShaders();
    std::shared_ptr<Shader> LoadShaderProgram(const fs::path& vertexShaderPath, const fs::path& fragmentShaderPath);
    std::shared_ptr<Shader> LoadShaderProgramFromMemory(const char* vertexShader, const char* fragmentShader);
    void UnloadShader(const std::shared_ptr<Shader>& shader) noexcept;
    void UnloadShader(GLuint shaderID) noexcept;
    const GLint GetUniformLocation(const GLuint& shader, const char* name) noexcept;
    const GLint GetAttribLocation(const GLuint& shader, const char* name) noexcept;
};

extern ShaderManager shaderManager;

#endif // SHADERS_HPP