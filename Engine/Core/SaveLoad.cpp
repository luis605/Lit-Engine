#ifndef GAME_SHIPPING
    #include "../include_all.h"
#endif

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
    struct adl_serializer<Vector3> {
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
    struct nlohmann::adl_serializer<SurfaceMaterial> {
        static void to_json(json& j, const SurfaceMaterial& material) {
            j = json{
                { "shininess", material.shininess },
                { "SpecularIntensity", material.SpecularIntensity },
                { "Roughness", material.Roughness },
                { "DiffuseIntensity", material.DiffuseIntensity },
                { "SpecularTint", material.SpecularTint }
            };
        }

        static void from_json(const json& j, SurfaceMaterial& material) {
            j.at("shininess").get_to(material.shininess);
            j.at("SpecularIntensity").get_to(material.SpecularIntensity);
            j.at("Roughness").get_to(material.Roughness);
            j.at("DiffuseIntensity").get_to(material.DiffuseIntensity);
            j.at("SpecularTint").get_to(material.SpecularTint);
        }
    };
}

void SaveEntity(json& json_data, const Entity& entity) {
    json j;
    j["type"] = "entity";
    j["color"] = entity.color;
    j["name"] = entity.name;
    j["scale"] = entity.scale;
    j["position"] = entity.position;
    j["relative_position"] = entity.relative_position;
    j["model_path"] = entity.model_path;
    j["script_path"] = entity.script;
    j["texture_path"] = entity.texture_path;
    j["normal_texture_path"] = entity.normal_texture_path;
    j["roughness_texture_path"] = entity.roughness_texture_path;
    j["id"] = entity.id;
    j["surface_material"] = entity.surface_material;

    if (!entity.children.empty()) {
        json children_data;
        for (const Entity* child : entity.children) {
            json child_json;
            SaveEntity(child_json, *child);
            children_data.emplace_back(child_json);
        }
        j["children"] = children_data;
    }

    json_data.emplace_back(j);
}



void SaveLight(json& json_data, const Light& light, int light_index) {
    json j;
    j["type"] = "light";
    j["color"] = light.color;
    j["name"] = lights_info.at(light_index).name;
    j["position"] = light.position;
    
    if (light.isChild)
        j["relative_position"] = light.relative_position;
    else
    {
        j["relative_position"]["x"] = 0;
        j["relative_position"]["y"] = 0;
        j["relative_position"]["z"] = 0;
    }
    
    j["target"] = light.target;
    j["direction"] = light.direction;
    j["intensity"] = light.intensity;
    j["cutOff"] = light.cutOff;
    j["specularStrength"] = light.specularStrength;
    j["attenuation"] = light.attenuation;
    j["id"] = lights_info.at(light_index).id;
    j["light_type"] = lights.at(light_index).type;


    json_data.emplace_back(j);
}

int SaveProject() {
    json json_data;
    for (const auto& entity : entities_list_pregame) {
        SaveEntity(json_data, entity);
    }

    int light_index = 0;
    for (const auto& light : lights) {
        SaveLight(json_data, light, light_index);
        light_index++;
    }

    std::ofstream outfile("project.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    outfile << std::setw(4) << json_data;
    outfile.close();

    return 0;
}






void LoadEntity(const json& entity_json, Entity& entity) {
    entity.setColor(Color{
        entity_json["color"]["r"].get<int>(),
        entity_json["color"]["g"].get<int>(),
        entity_json["color"]["b"].get<int>(),
        entity_json["color"]["a"].get<int>()
    });
    
    entity.setName(entity_json["name"].get<std::string>());

    Vector3 scale{
        entity_json["scale"]["x"].get<float>(),
        entity_json["scale"]["y"].get<float>(),
        entity_json["scale"]["z"].get<float>()
    };
    entity.setScale(scale);

    Vector3 position{
        entity_json["position"]["x"].get<float>(),
        entity_json["position"]["y"].get<float>(),
        entity_json["position"]["z"].get<float>()
    };
    entity.position = position;

    Vector3 relative_position{
        entity_json["relative_position"]["x"].get<float>(),
        entity_json["relative_position"]["y"].get<float>(),
        entity_json["relative_position"]["z"].get<float>()
    };
    entity.relative_position = relative_position;

    entity.setModel(entity_json["model_path"].get<std::string>().c_str());
    entity.script = entity_json["script_path"].get<std::string>();
    entity.id = entity_json["id"].get<int>();

    // Materials
    entity.surface_material.shininess = entity_json["surface_material"]["shininess"].get<float>();
    entity.surface_material.SpecularIntensity = entity_json["surface_material"]["SpecularIntensity"].get<float>();
    entity.surface_material.Roughness = entity_json["surface_material"]["Roughness"].get<float>();
    entity.surface_material.DiffuseIntensity = entity_json["surface_material"]["DiffuseIntensity"].get<float>();

    glm::vec3 SpecularTintValue;
    const json& SpecularTintJson = entity_json["surface_material"]["SpecularTint"];
    SpecularTintValue.x = SpecularTintJson["x"].get<float>();
    SpecularTintValue.y = SpecularTintJson["y"].get<float>();
    SpecularTintValue.z = SpecularTintJson["z"].get<float>();
    entity.surface_material.SpecularTint = SpecularTintValue;

    // Textures
    string texture_path = entity_json["texture_path"].get<std::string>();
    if (!texture_path.empty())
    {
        entity.texture_path = texture_path;
        entity.texture = LoadTexture(entity.texture_path.c_str());
        entity.ReloadTextures();
    }

    entity.normal_texture_path = entity_json["normal_texture_path"].get<std::string>();
    if (!entity.normal_texture_path.empty())
    {
        entity.normal_texture = LoadTexture(entity.normal_texture_path.c_str());
        entity.ReloadTextures();
    }

    entity.roughness_texture_path = entity_json["roughness_texture_path"].get<std::string>();
    if (!entity.roughness_texture_path.empty())
    {
        entity.roughness_texture = LoadTexture(entity.roughness_texture_path.c_str());
        entity.ReloadTextures();
    }


    entity.setShader(shader);


    if (entity_json.contains("children")) {
        const json& children_data = entity_json["children"];
        if (children_data.is_array() && !children_data.empty()) {
            for (const auto& child_json : children_data[0]) {
                Entity* child = new Entity();
                LoadEntity(child_json, *child);
                entity.children.push_back(child);
            }
        }
    }
}



void LoadLight(const json& light_json, Light& light, AdditionalLightInfo light_info) {
    light.color = (glm::vec4{
        light_json["color"]["r"].get<int>(),
        light_json["color"]["g"].get<int>(),
        light_json["color"]["b"].get<int>(),
        light_json["color"]["a"].get<int>()
    });
    
    light_info.name = (light_json["name"].get<std::string>());

    glm::vec3 position{
        light_json["position"]["x"].get<float>(),
        light_json["position"]["y"].get<float>(),
        light_json["position"]["z"].get<float>()
    };

    light.position = position;

    glm::vec3 relative_position{
        light_json["relative_position"]["x"].get<float>(),
        light_json["relative_position"]["y"].get<float>(),
        light_json["relative_position"]["z"].get<float>()
    };

    light.relative_position = relative_position;

    glm::vec3 target{
        light_json["target"]["x"].get<float>(),
        light_json["target"]["y"].get<float>(),
        light_json["target"]["z"].get<float>()
    };

    light.target = target;
    
    glm::vec3 direction{
        light_json["direction"]["x"].get<float>(),
        light_json["direction"]["y"].get<float>(),
        light_json["direction"]["z"].get<float>()
    };

    light.direction = direction;
    
    light_info.id = light_json["id"].get<int>();

    light.intensity          = light_json["intensity"].get<float>();
    light.cutOff             = light_json["cutOff"].get<float>();
    light.specularStrength   = light_json["specularStrength"].get<float>();
    light.attenuation        = light_json["attenuation"].get<float>();

    light.type               = (LightType)light_json["light_type"].get<int>();
}


int LoadProject(vector<Entity>& entities_vector, vector<Light>& lights_vector, vector<AdditionalLightInfo>& lights_info_vector) {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    json json_data;
    infile >> json_data;

    infile.close();

    entities_vector.clear();
    lights_vector.clear();
    lights_info_vector.clear();
    
    try {
        for (const auto& entity_json : json_data) {
            string type = entity_json["type"].get<std::string>();
            if (type == "entity") {
                Entity entity;
                LoadEntity(entity_json, entity);
                entities_vector.push_back(entity);
            }
            else if (type == "light") {
                Light light;
                AdditionalLightInfo light_info;
                LoadLight(entity_json, light, light_info);
                lights_info_vector.push_back(light_info);
                lights_vector.push_back(light);
            }
        }
    } catch (const json::type_error& e) {
        std::cerr << "JSON type error: " << e.what() << std::endl;
        return 1;
    }

    UpdateLightsBuffer(false, lights_vector);
    
    return 0;
}
