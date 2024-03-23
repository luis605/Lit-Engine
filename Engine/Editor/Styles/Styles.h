#include "../../include_all.h"


std::string toHexString(ImU32 color);

void LoadThemeFromFile(const std::string& filename);

enum FileExplorerType
{
    Save,
    Load,
    SaveLoad
};

ImGuiCol_ theme_create_selected_option = ImGuiCol_Text;

std::vector<int> newThemeSavedOptions;
std::vector<ImVec4> newThemeSavedOptionsColor;

void CreateNewTheme();

void SetStyleHighContrast(ImGuiStyle* dst);

ImVec4 rgbaToImguiColor(int red, int green, int blue, int alpha);

void SetStyleGray(ImGuiStyle* dst);