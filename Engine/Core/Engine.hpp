#ifndef ENGINE_H
#define ENGINE_H

#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Raycast.hpp>
#include <Engine/GUI/Button/Button.hpp>
#include <Engine/GUI/Text/Text.hpp>
#include <Engine/Lighting/lights.hpp>
#include <filesystem>
#include <glad.h>
#include <string>
#include <unordered_map>

namespace fs = std::filesystem;

struct LightStruct;
struct SurfaceMaterial;
struct SurfaceMaterialTexture;

extern std::vector<Entity> entitiesListPregame;
extern std::vector<Entity> entitiesList;
extern std::unordered_map<int, size_t> entityIdToIndexMap;
extern std::unordered_map<int, size_t> lightIdToIndexMap;
extern std::unordered_map<GLuint, std::unordered_map<std::string, GLint>>
    uniformLocationCache;

extern Entity* selectedEntity;
extern LightStruct* selectedLight;
extern LitButton* selectedButton;
extern Text* selectedTextElement;
extern fs::path selectedMaterial;

std::string readFileToString(const std::string& filename);
const char* decryptFileString(const std::string& inputFile,
                              const std::string& key);
const char* encryptFileString(const std::string& inputFile,
                              const std::string& key);

#endif // ENGINE_H