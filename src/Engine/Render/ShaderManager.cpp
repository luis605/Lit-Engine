module;
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
module Engine.Render.shaderManager;

import Engine.shader;

uint32_t ShaderManager::loadShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath) {
    const std::filesystem::path combinedPath = vertexPath / fragmentPath;
    if (m_shaderPathToId.contains(combinedPath)) {
        return m_shaderPathToId.at(combinedPath);
    }

    auto shader = std::make_unique<Shader>(vertexPath, fragmentPath);
    if (shader->isInitialized()) {
        const uint32_t shaderId = m_shaders.size();
        m_shaders.push_back(std::move(shader));
        m_shaderPathToId[combinedPath] = shaderId;
        return shaderId;
    }
    return -1;
}

uint32_t ShaderManager::loadComputeShader(const std::filesystem::path& computePath) {
    if (m_shaderPathToId.contains(computePath)) {
        return m_shaderPathToId.at(computePath);
    }

    auto shader = std::make_unique<Shader>(computePath);
    if (shader->isInitialized()) {
        const uint32_t shaderId = m_shaders.size();
        m_shaders.push_back(std::move(shader));
        m_shaderPathToId[computePath] = shaderId;
        return shaderId;
    }
    return -1;
}

Shader* ShaderManager::getShader(uint32_t shaderId) {
    if (shaderId < m_shaders.size()) {
        return m_shaders[shaderId].get();
    }
    return nullptr;
}

size_t ShaderManager::getShaderCount() const { return m_shaders.size(); }

void ShaderManager::cleanup() {
    for (auto& shader : m_shaders) {
        shader->release();
    }

    m_shaders.clear();
    m_shaderPathToId.clear();
}