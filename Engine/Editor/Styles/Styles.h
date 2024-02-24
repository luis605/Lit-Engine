#include "../../include_all.h"


std::string to_hex_string(ImU32 color);

void LoadThemeFromFile(const std::string& filename);

enum FileExplorerType
{
    Save,
    Load,
    SaveLoad
};

string showFileExplorer(const char* folderPath, nlohmann::json fileContent, FileExplorerType type = FileExplorerType::Save);

ImGuiCol_ selectedThemeCreateOption = ImGuiCol_Text;

vector<int> newThemeSavedOptions;
vector<ImVec4> newThemeSavedOptionsColor;

void CreateNewTheme();

void SetStyleHighContrast(ImGuiStyle* dst);

ImVec4 rgbaToImguiColor(int red, int green, int blue, int alpha);

void SetStyleGray(ImGuiStyle* dst);