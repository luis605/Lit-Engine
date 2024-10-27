#ifndef SAVE_LOAD_H
#define SAVE_LOAD_H

std::map<std::string, std::string> scriptContents;

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


    template<>
    struct adl_serializer<SurfaceMaterial> {
        static void to_json(json& j, const SurfaceMaterial& material) {
            j = json{
                { "SpecularIntensity", material.SpecularIntensity },
                { "DiffuseIntensity", material.DiffuseIntensity },
                { "Roughness", material.Roughness }
            };
        }

        static void from_json(const json& j, SurfaceMaterial& material) {
            j.at("SpecularIntensity").get_to(material.SpecularIntensity);
            j.at("DiffuseIntensity").get_to(material.DiffuseIntensity);
            j.at("Roughness").get_to(material.Roughness);
        }
    };
}

inline bool isSubpath(const fs::path &path, const fs::path &base);
void SerializeMaterial(SurfaceMaterial& material, const fs::path path);
void DeserializeMaterial(SurfaceMaterial* material, const fs::path& path);
std::string serializePythonScript(const fs::path& path);
void serializeScripts();

void SaveCamera(json& jsonData, const LitCamera camera);
void SaveEntity(json& jsonData, const Entity& entity);
void SaveLight(json& jsonData, const LightStruct& lightStruct);
void SaveText(json& jsonData, const Text& text, bool emplaceBack = true);
void SaveWorldSetting(json& jsonData);
void SaveButton(json& jsonData, const LitButton& button);
int SaveProject();

void LoadCamera(const json& cameraJson, LitCamera& camera);
void LoadWorldSettings(const json& worldSettingsJson);
Entity* LoadEntity(const json& entityJson);
LightStruct& LoadLight(const json& lightJson);
void LoadText(const json& textJson, Text& text);
void LoadButton(const json& buttonJson, LitButton& button);
int LoadProject(std::vector<Entity>& entitiesVector, std::vector<LightStruct>& lightsVector, LitCamera& camera);

#endif // SAVE_LOAD_H