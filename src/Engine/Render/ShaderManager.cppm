module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

export module Engine.Render.shaderManager;

import Engine.shader;

export class ShaderManager {
  public:
    ShaderManager() = default;
    ~ShaderManager() = default;

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;
    ShaderManager(ShaderManager&&) = delete;
    ShaderManager& operator=(ShaderManager&&) = delete;

    uint32_t loadShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    uint32_t loadComputeShader(const std::filesystem::path& computePath);

    Shader* getShader(uint32_t shaderId);
    size_t getShaderCount() const;

    void cleanup();

  private:
    std::vector<std::unique_ptr<Shader>> m_shaders;
    std::unordered_map<std::filesystem::path, uint32_t> m_shaderPathToId;
};
