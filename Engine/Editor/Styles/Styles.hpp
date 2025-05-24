/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef EDITOR_STYLES_H
#define EDITOR_STYLES_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

enum FileExplorerType { Save, Load, SaveLoad };

extern ImGuiCol_ theme_create_selected_option;
extern std::vector<int> newThemeSavedOptions;
extern std::vector<ImVec4> newThemeSavedOptionsColor;
extern const int themes_colors[];
extern const char* themes_colors_string[];

ImVec4 rgbaToImguiColor(int red, int green, int blue, int alpha);
std::string toHexString(ImU32 color);
void LoadThemeFromFile(const std::string& filename);
void CreateNewTheme();
void SetStyleHighContrast(ImGuiStyle* dst);
void SetStyleGray(ImGuiStyle* dst);

#endif // EDITOR_STYLES_H