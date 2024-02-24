#include "../../include_all.h"


std::string to_hex_string(ImU32 color);

void LoadThemeFromFile(const std::string& filename);

enum FileExplorerType
{
    Save,
    Load,
    SaveLoad
};

ImGuiCol_ theme_create_selected_option = ImGuiCol_Text;

vector<int> new_theme_saved_options;
vector<ImVec4> new_theme_saved_options_color;

void CreateNewTheme();

void SetStyleHighContrast(ImGuiStyle* dst);

ImVec4 rgbaToImguiColor(int red, int green, int blue, int alpha);

void SetStyleGray(ImGuiStyle* dst);