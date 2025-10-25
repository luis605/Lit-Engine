module;

#include <glad/glad.h>

module Engine.shader;

import Engine.glm;
import std;

static std::string LoadSourceFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "ERROR::SHADER: Failed to open file: " << filepath << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static unsigned int CompileShader(const std::string& source, GLenum type) {
    unsigned int id = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    int result;
    glGetShaderiv(id, GL_COMPILE_STATUS, &result);
    if (result == GL_FALSE) {
        int length;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> message(length);
        glGetShaderInfoLog(id, length, &length, message.data());

        const char* shaderTypeStr = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
        std::cerr << "ERROR::SHADER::" << shaderTypeStr << "::COMPILATION_FAILED\n"
                  << message.data() << std::endl;

        glDeleteShader(id);
        return 0;
    }

    return id;
}

Shader::Shader(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
    std::string vertexSource = LoadSourceFromFile(vertexShaderPath);
    std::string fragmentSource = LoadSourceFromFile(fragmentShaderPath);

    if (!vertexSource.empty() && !fragmentSource.empty()) {
        createAndLink(vertexSource, fragmentSource);
    }
}

Shader::~Shader() {
    if (m_initialized) {
        release();
    }
}

Shader::Shader(Shader&& other) noexcept
    : m_shaderID(other.m_shaderID), m_initialized(other.m_initialized),
      m_uniformLocations(std::move(other.m_uniformLocations)) {

    other.m_shaderID = 0;
    other.m_initialized = false;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {

        if (m_initialized) {
            release();
        }

        m_shaderID = other.m_shaderID;
        m_initialized = other.m_initialized;
        m_uniformLocations = std::move(other.m_uniformLocations);

        other.m_shaderID = 0;
        other.m_initialized = false;
    }
    return *this;
}

void Shader::reload(const std::string& vertexShaderPath, const std::string& fragmentShaderPath) {
    if (m_initialized) {
        release();
    }

    std::string vertexSource = LoadSourceFromFile(vertexShaderPath);
    std::string fragmentSource = LoadSourceFromFile(fragmentShaderPath);

    if (!vertexSource.empty() && !fragmentSource.empty()) {
        createAndLink(vertexSource, fragmentSource);
    }
}

void Shader::bind() const {
    if (m_initialized) {
        glUseProgram(m_shaderID);
    }
}

void Shader::unbind() const { glUseProgram(0); }

void Shader::setUniform(const std::string& name, int value) const {
    glUniform1i(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string& name, float value) const {
    glUniform1f(getUniformLocation(name), value);
}

void Shader::setUniform(const std::string& name, const glm::vec3& vector) const {
    glUniform3fv(getUniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::setUniform(const std::string& name, const glm::mat4& matrix) const {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

int Shader::getUniformLocation(const std::string& name) const {
    if (!m_initialized)
        return -1;

    if (m_uniformLocations.count(name)) {
        return m_uniformLocations.at(name);
    }

    int location = glGetUniformLocation(m_shaderID, name.c_str());
    if (location == -1) {

        std::cerr << "WARNING::SHADER: Uniform '" << name << "' not found!" << std::endl;
    }

    m_uniformLocations[name] = location;
    return location;
}

void Shader::createAndLink(const std::string& vertexSrc, const std::string& fragmentSrc) {
    unsigned int vertexShader = CompileShader(vertexSrc, GL_VERTEX_SHADER);
    unsigned int fragmentShader = CompileShader(fragmentSrc, GL_FRAGMENT_SHADER);

    if (vertexShader == 0 || fragmentShader == 0) {

        if (vertexShader != 0)
            glDeleteShader(vertexShader);
        if (fragmentShader != 0)
            glDeleteShader(fragmentShader);
        return;
    }

    m_shaderID = glCreateProgram();
    glAttachShader(m_shaderID, vertexShader);
    glAttachShader(m_shaderID, fragmentShader);
    glLinkProgram(m_shaderID);

    int result;
    glGetProgramiv(m_shaderID, GL_LINK_STATUS, &result);
    if (result == GL_FALSE) {
        int length;
        glGetProgramiv(m_shaderID, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> message(length);
        glGetProgramInfoLog(m_shaderID, length, &length, message.data());
        std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << message.data() << std::endl;

        glDeleteProgram(m_shaderID);
        m_shaderID = 0;
    } else {
        m_initialized = true;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

void Shader::release() {
    glDeleteProgram(m_shaderID);
    m_shaderID = 0;
    m_initialized = false;
    m_uniformLocations.clear();
}