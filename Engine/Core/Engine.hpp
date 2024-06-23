#ifndef ENGINE_H
#define ENGINE_H

class Entity;
struct LightStruct;

std::vector<Entity> entitiesListPregame;
std::vector<Entity> entitiesList;

Entity*      selectedEntity      = nullptr;
LightStruct* selectedLight       = nullptr;
LitButton*   selectedButton      = nullptr;
Text*        selectedTextElement = nullptr;
fs::path     selectedMaterial;

struct HitInfo {
    bool hit;
    LitVector3 worldPoint;
    LitVector3 relativePoint; // Relative Hit Position from Origin
    LitVector3 worldNormal;
    float distance;
    Color hitColor;
    Entity* entity;
};

std::string getFileExtension(const std::string& filePath);
std::string colorToString(const Color& color);
std::string readFileToString(const std::string& filename);
const char* decryptFileString(const std::string& inputFile, const std::string& key);
const char* encryptFileString(const std::string& inputFile, const std::string& key);
HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore);

#endif // ENGINE_H