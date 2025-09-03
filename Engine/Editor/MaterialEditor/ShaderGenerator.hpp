/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/
#ifndef MATERIAL_SHADER_GENERATOR_HPP
#define MATERIAL_SHADER_GENERATOR_HPP

#include <string>
#include <vector>
#include <set>
#include "LivingMaterial.hpp"

struct ShaderGenReport {
    std::string codegenVersion = "1.0.0";
    int numLayers = 0;
    int numSamplers = 0;
    std::vector<std::string> envUniforms;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

std::string GenerateFragmentShader(const LivingMaterial& mat, ShaderGenReport* outReport);

#endif // MATERIAL_SHADER_GENERATOR_HPP