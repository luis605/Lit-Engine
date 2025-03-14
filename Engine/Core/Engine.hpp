#ifndef ENGINE_H
#define ENGINE_H
#include "Entity.hpp"

struct LightStruct;

std::vector<Entity> entitiesListPregame;
std::vector<Entity> entitiesList;
std::unordered_map<int, size_t> entityIdToIndexMap;
std::unordered_map<int, size_t> lightIdToIndexMap;
std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> uniformLocationCache;

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

std::string colorToString(const Color& color);
std::string readFileToString(const std::string& filename);
const char* decryptFileString(const std::string& inputFile, const std::string& key);
const char* encryptFileString(const std::string& inputFile, const std::string& key);
HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore);
GLint GetUniformLocation(Shader& shader, const char* name);
GLint GetAttribLocation(Shader& shader, const char* name);

#endif // ENGINE_H