#ifndef MATERIAL_EDITOR_HPP
#define MATERIAL_EDITOR_HPP

#include <extras/IconsFontAwesome6.h>
#include <imgui.h>
#include <filesystem>

namespace fs = std::filesystem;

extern bool isMaterialEditorOpen;
extern fs::path selectedMaterialBlueprintPath;

void MaterialEditor() noexcept;

#endif // MATERIAL_EDITOR_HPP