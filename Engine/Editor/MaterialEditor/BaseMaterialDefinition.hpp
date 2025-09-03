/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef BASE_MATERIAL_DEFINITION_HPP
#define BASE_MATERIAL_DEFINITION_HPP

#include <string>
#include <vector>
#include <map>
#include <Engine/Core/Math.hpp>
#include <Engine/Core/Filesystem.hpp>

struct BaseMaterialDefinition {
    fs::path m_AlbedoMapPath;
    fs::path m_NormalMapPath;
    fs::path m_RoughnessMapPath;
    fs::path m_MetalnessMapPath;
    fs::path m_AOMapPath;

    std::map<std::string, Vector4> m_DefaultParameters;
};

#endif // BASE_MATERIAL_DEFINITION_HPP