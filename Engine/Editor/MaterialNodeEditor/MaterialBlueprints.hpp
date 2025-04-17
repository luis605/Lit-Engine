#ifndef MATERIAL_BLUEPRINTS_HPP
#define MATERIAL_BLUEPRINTS_HPP

#include <Engine/Lighting/SurfaceMaterial.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <unordered_map>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct MaterialBlueprint {
    std::string name;
    MaterialNodeSystem nodeSystem;
    fs::path materialPath;
    std::string UUID;
};

extern std::unordered_map<fs::path, MaterialBlueprint> materialBlueprints;

void LoadMaterialBlueprint(const fs::path& filePath);
void SaveMaterialBlueprints(const fs::path& filePath, const MaterialBlueprint& blueprintMaterial);
void CreateMaterialBlueprint(const std::string& name, const fs::path& path);

#endif // MATERIAL_BLUEPRINTS_HPP