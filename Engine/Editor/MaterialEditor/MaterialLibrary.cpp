/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "MaterialLibrary.hpp"
#include <fstream>
#include "nlohmann/json.hpp"
#include <stdexcept>

using json = nlohmann::json;

void MaterialLibrary::LoadLibrary(const fs::path& libraryPath) {
    std::ifstream f(libraryPath);
    if (!f.is_open()) {
        throw std::runtime_error("MaterialLibrary::LoadLibrary - failed to open: " + libraryPath.string());
    }
    json data = json::parse(f, nullptr, true, true);
    f.close();

    m_Materials.clear();

    for (auto& [id, material] : data.items()) {
        BaseMaterialDefinition def;

        if (material.contains("albedo") && !material["albedo"].is_null()) {
            def.m_AlbedoMapPath = fs::path(material["albedo"].get<std::string>());
        }
        if (material.contains("normal") && !material["normal"].is_null()) {
            def.m_NormalMapPath = fs::path(material["normal"].get<std::string>());
        }
        if (material.contains("roughness") && !material["roughness"].is_null()) {
            def.m_RoughnessMapPath = fs::path(material["roughness"].get<std::string>());
        }

        if (material.contains("parameters") && material["parameters"].is_object()) {
            for (auto& [paramName, paramValue] : material["parameters"].items()) {
                if (paramValue.is_array() && paramValue.size() == 4) {
                    def.m_DefaultParameters[paramName] = {
                        paramValue[0].get<float>(),
                        paramValue[1].get<float>(),
                        paramValue[2].get<float>(),
                        paramValue[3].get<float>()
                    };
                }
            }
        }
        m_Materials[id] = def;
    }
}

BaseMaterialDefinition& MaterialLibrary::getDefinition(const std::string& id) {
    if (m_Materials.count(id)) {
        return m_Materials.at(id);
    }
    throw std::runtime_error("Material definition not found for ID: " + id);
}

std::vector<std::string> MaterialLibrary::GetAvailableMaterialIDs() const {
    std::vector<std::string> ids;
    ids.reserve(m_Materials.size());
    for (const auto& [id, _] : m_Materials) {
        ids.push_back(id);
    }
    return ids;
}