module;

#include <string>
#include <unordered_map>

import Engine.glm;

export module Engine.shader;

export class Shader {
  public:
    Shader(const std::string& computeShaderPath);
    Shader(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    void reload(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);

    void bind() const;
    void unbind() const;
    bool isInitialized() const { return m_initialized; }

    void setUniform(const std::string& name, int value) const;
    void setUniform(const std::string& name, unsigned int value) const;
    void setUniform(const std::string& name, float value) const;
    void setUniform(const std::string& name, const glm::vec3& vector) const;
    void setUniform(const std::string& name, const glm::mat4& matrix) const;

  private:
    int getUniformLocation(const std::string& name) const;
    void createAndLink(const std::string& computeSrc);
    void createAndLink(const std::string& vertexSrc, const std::string& fragmentSrc);
    void release();

    unsigned int m_shaderID = 0;
    bool m_initialized = false;

    mutable std::unordered_map<std::string, int> m_uniformLocations;
};