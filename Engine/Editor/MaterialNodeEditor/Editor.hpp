/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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