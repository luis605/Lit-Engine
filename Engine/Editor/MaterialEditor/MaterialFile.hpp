#ifndef SAVE_MATERIAL_HPP
#define SAVE_MATERIAL_HPP

#include <filesystem>
#include "LivingMaterial.hpp"
namespace fs = std::filesystem;

namespace MaterialFile {
    void Save(const fs::path& materialPath, const LivingMaterial& material);
    void Load(const fs::path& materialPath);
};

#endif // SAVE_MATERIAL_HPP