/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef SAVE_LOAD_H
#define SAVE_LOAD_H

#include <nlohmann/json.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/GUI/Text/Text.hpp>
#include <Engine/GUI/Button/Button.hpp>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Core/Entity.hpp>
#include <raylib.h>
#include <glm/glm.hpp>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

extern std::map<std::string, std::string> scriptContents;

namespace nlohmann {
    template<>
    struct adl_serializer<Color> {
        static void to_json(json& j, const Color& color) {
            j = json{{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
        }

        static void from_json(const json& j, Color& color) {
            color.r = j["r"];
            color.g = j["g"];
            color.b = j["b"];
            color.a = j["a"];
        }
    };

    template<>
    struct adl_serializer<glm::vec4> {
        static void to_json(json& j, const glm::vec4& vec) {
            j = json{{"r", vec.r}, {"g", vec.g}, {"b", vec.b}, {"a", vec.a}};
        }

        static void from_json(const json& j, glm::vec4& vec) {
            vec.r = j["r"];
            vec.g = j["g"];
            vec.b = j["b"];
            vec.a = j["a"];
        }
    };


    template<>
    struct adl_serializer<Vector2> {
        static void to_json(json& j, const Vector2& vec) {
            j = json{{"x", vec.x}, {"y", vec.y}};
        }

        static void from_json(const json& j, Vector2& vec) {
            vec.x = j["x"];
            vec.y = j["y"];
        }
    };


    template<>
    struct adl_serializer<Vector4> {
        static void to_json(json& j, const Vector4& vec) {
            j = json{{"x", vec.x}, {"y", vec.y}, {"z", vec.z}, {"w", vec.w}};
        }

        static void from_json(const json& j, Vector4& vec) {
            vec.x = j["x"];
            vec.y = j["y"];
            vec.z = j["z"];
            vec.w = j["w"];
        }
    };


    template<>
    struct adl_serializer<LitVector3> {
        static void to_json(json& j, const Vector3& vec) {
            j = json{{"x", vec.x}, {"y", vec.y}, {"z", vec.z}};
        }

        static void from_json(const json& j, Vector3& vec) {
            vec.x = j["x"];
            vec.y = j["y"];
            vec.z = j["z"];
        }
    };


    template<>
    struct adl_serializer<glm::vec3> {
        static void to_json(json& j, const glm::vec3& vec) {
            j = json{{"x", vec.x}, {"y", vec.y}, {"z", vec.z}};
        }

        static void from_json(const json& j, glm::vec3& vec) {
            vec.x = j["x"];
            vec.y = j["y"];
            vec.z = j["z"];
        }
    };
}

std::string serializePythonScript(const fs::path& path);
void serializeScripts();

bool isSubPath(const fs::path &path, const fs::path &base);
void SaveCamera(json& jsonData, const LitCamera& camera);
void SaveEntity(json& jsonData, const Entity& entity);
void SaveLight(json& jsonData, const LightStruct& lightStruct);
void SaveWorldSetting(json& jsonData);
void SaveText(json& jsonData, const Text& text, const bool& emplaceBack = true);
void SaveButton(json& jsonData, const LitButton& button);
int SaveProject();

void LoadCamera(const json& cameraJson, LitCamera& camera);
void LoadWorldSettings(const json& worldSettingsJson);
Entity LoadEntity(const json& entityJson);
LightStruct& LoadLight(const json& lightJson);
void LoadText(const json& textJson, Text& text);
void LoadButton(const json& buttonJson, LitButton& button);
int LoadProject(std::vector<Entity>& entitiesVector, std::vector<LightStruct>& lightsVector, LitCamera& camera);

#endif // SAVE_LOAD_H