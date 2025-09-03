/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/
#ifndef MATERIAL_SHADER_CACHE_HPP
#define MATERIAL_SHADER_CACHE_HPP

#include <string>
#include <filesystem>
#include "LivingMaterial.hpp"
namespace fs = std::filesystem;

struct ShaderCacheResult {
    fs::path cachedPath;
    std::string hashHex;
    bool ok = false;
    std::string error;
};

ShaderCacheResult CacheCompiledShader(const fs::path& materialJsonPath,
                                      const LivingMaterial& material,
                                      const std::string& shaderSource,
                                      const std::string& codegenVersion);

#endif // MATERIAL_SHADER_CACHE_HPP