/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_LIBRARY_HPP
#define MATERIAL_LIBRARY_HPP

#include <string>
#include <map>
#include "BaseMaterialDefinition.hpp"

class MaterialLibrary {
public:
    static MaterialLibrary& GetInstance() {
        static MaterialLibrary instance;
        return instance;
    }

    MaterialLibrary(const MaterialLibrary&) = delete;
    MaterialLibrary& operator=(const MaterialLibrary&) = delete;

    void LoadLibrary(const fs::path& libraryPath);
    BaseMaterialDefinition& getDefinition(const std::string& id);
    std::vector<std::string> GetAvailableMaterialIDs() const;

private:
    MaterialLibrary() = default;
    std::map<std::string, BaseMaterialDefinition> m_Materials;
};

#endif // MATERIAL_LIBRARY_HPP