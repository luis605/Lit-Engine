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


void SaveEntity(json& json_data, const Entity& entity);
void SaveLight(json& json_data, const Light& light, int light_index);
void SaveText(json& json_data, const Text& text, bool emplace_back = true);
void SaveButton(json& json_data, const LitButton& button);

void SaveEntity(json& json_data, const Entity& entity) {
    json j;
    j["type"] = "entity";
    j["color"] = entity.color;
    j["name"] = entity.name;
    j["scale"] = entity.scale;
    j["position"] = entity.position;
    j["rotation"] = entity.rotation;
    j["relative_position"] = entity.relative_position;
    j["model_path"] = entity.model_path;
    j["script_path"] = entity.script;
    j["texture_path"] = entity.texture_path;
    j["normal_texture_path"] = entity.normal_texture_path;
    j["roughness_texture_path"] = entity.roughness_texture_path;
    j["id"] = entity.id;
    j["surface_material"] = entity.surface_material;
    j["is_dynamic"] = entity.isDynamic;
    j["mass"] = entity.mass;

    if (!entity.children.empty()) {
        json children_data;

        std::vector<std::variant<Entity*, Light*, Text*, LitButton*>> nonConstChildren = entity.children;
        for (std::variant<Entity*, Light*, Text*, LitButton*>& childVariant : nonConstChildren)
        {
            if (std::holds_alternative<Entity*>(childVariant))
            {
                Entity* child = std::get<Entity*>(childVariant);
                json child_json;
                SaveEntity(child_json, *child);
                children_data.emplace_back(child_json);
            }
            else if (std::holds_alternative<Light*>(childVariant))
            {
                Light* child = std::get<Light*>(childVariant);
                json child_json;

                int light_index = -1;
                auto light_it = std::find_if(lights.begin(), lights.end(), [&child, &light_index](const Light& light) {
                    light_index++;
                    return light.id == child->id;
                });


                SaveLight(child_json, *child, light_index);
                children_data.emplace_back(child_json);
            }

        }
        j["children"] = children_data;
    }

    json_data.emplace_back(j);
}


void SaveLight(json& json_data, const Light& light, int light_index) {
    json j;
    j["type"] = "light";
    j["color"]["r"] = light.color.r * 255;
    j["color"]["g"] = light.color.g * 255;
    j["color"]["b"] = light.color.b * 255;
    j["color"]["a"] = light.color.a * 255;
    
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
    j["isChild"] = light.isChild;
    j["id"] = lights_info.at(light_index).id;
    j["light_type"] = lights.at(light_index).type;


    json_data.emplace_back(j);
}


void SaveText(json& json_data, const Text& text, bool emplace_back = true) {
    json j;
    j["type"] = "text";
    j["name"] = text.name;
    j["text"] = text.text;
    j["color"] = text.color;
    j["background color"] = text.backgroundColor;
    j["background roundiness"] = text.backgroundRoundness;
    j["position"] = text.position;
    j["font size"] = text.fontSize;
    j["spacing"] = text.spacing;
    j["padding"] = text.padding;

    if (emplace_back)
        json_data.emplace_back(j);
    else
       json_data = j;

}


void SaveButton(json& json_data, const LitButton& button) {
    json j;
    j["type"] = "button";
    
    // Create a JSON object for the "text" field
    json text_json;
    SaveText(text_json, button.text, false);
    j["text"] = text_json;

    j["name"] = button.name;
    j["position"] = button.position;
    j["size"] = button.size;
    j["color"] = button.color;
    j["pressed color"] = button.pressedColor;
    j["hover color"] = button.hoverColor;
    j["disabled color"] = button.disabledButtonColor;
    j["disabled text color"] = button.disabledText;
    j["disabled hover color"] = button.disabledHoverColor;;
    j["button roundness"] = button.roundness;
    j["auto resize"] = button.autoResize;

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

    for (const Text& text : textElements)
    {
        SaveText(json_data, text);
    }

    for (const LitButton& button : lit_buttons)
    {
        SaveButton(json_data, button);
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

void LoadEntity(const json& entity_json, Entity& entity);
void LoadLight(const json& light_json, Light& light, AdditionalLightInfo light_info);



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

    Vector3 rotation{
        entity_json["rotation"]["x"].get<float>(),
        entity_json["rotation"]["y"].get<float>(),
        entity_json["rotation"]["z"].get<float>()
    };
    entity.rotation = rotation;

    Vector3 relative_position{
        entity_json["relative_position"]["x"].get<float>(),
        entity_json["relative_position"]["y"].get<float>(),
        entity_json["relative_position"]["z"].get<float>()
    };
    entity.relative_position = relative_position;

    entity.isDynamic = entity_json["is_dynamic"].get<bool>();
    std::cout << "IS DYNAMIC: " << entity.isDynamic << "\n";
    entity.mass = entity_json["mass"].get<float>();


    entity.setModel(
        entity_json["model_path"].get<std::string>().c_str(),
        LoadModel(entity_json["model_path"].get<std::string>().c_str())
    );

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
        std::cout << "SET TEXTURE NORMAL\n";
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
        if (children_data.is_array()) {
            for (const auto& child_array : children_data) {
                if (!child_array.empty()) { // Check if the array is not empty
                    const json& child_json = child_array[0]; // Access the first object in the array
                    string type = child_json["type"].get<std::string>();
                    if (type == "entity") {
                        Entity* child = new Entity();
                        LoadEntity(child_json, *child);
                        entity.children.push_back(child);
                    } else if (type == "light") {
                        Light* child = new Light();
                        AdditionalLightInfo light_info;
                        LoadLight(child_json, *child, light_info);
                        lights.push_back(*child);
                        lights_info.push_back(light_info);
                        entity.addChild(&lights.back());
                    }
                }
            }
        }
    }
}



void LoadLight(const json& light_json, Light& light, AdditionalLightInfo light_info) {
    std::cout << "Creating Light\n";
    light.color = (glm::vec4{
        light_json["color"]["r"].get<int>() / 255,
        light_json["color"]["g"].get<int>() / 255,
        light_json["color"]["b"].get<int>() / 255,
        light_json["color"]["a"].get<int>() / 255
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
    light.isChild            = light_json["isChild"].get<bool>();
    light.type               = (LightType)light_json["light_type"].get<int>();
}


void LoadText(const json& text_json, Text& text) {
    text.name                = text_json["name"].get<std::string>();
    text.text                = text_json["text"].get<std::string>();

    text.color.a             = text_json["color"]["a"].get<unsigned char>();
    text.color.r             = text_json["color"]["r"].get<unsigned char>();
    text.color.g             = text_json["color"]["g"].get<unsigned char>();
    text.color.b             = text_json["color"]["b"].get<unsigned char>();

    text.backgroundColor.a             = text_json["background color"]["a"].get<unsigned char>();
    text.backgroundColor.r             = text_json["background color"]["r"].get<unsigned char>();
    text.backgroundColor.g             = text_json["background color"]["g"].get<unsigned char>();
    text.backgroundColor.b             = text_json["background color"]["b"].get<unsigned char>();

    text.backgroundRoundness          = text_json["background roundiness"].get<float>();
    text.fontSize                      = text_json["font size"].get<float>();
    text.spacing                       = text_json["spacing"].get<float>();
    text.padding                       = text_json["padding"].get<float>();

    text.position = {
        text_json["position"]["x"].get<float>(),
        text_json["position"]["y"].get<float>(),
        text_json["position"]["z"].get<float>()
    };
}


void LoadButton(const json& button_json, LitButton& button) {
    // Load text properties
    LoadText(button_json["text"], button.text);

    // Load other button properties
    button.name = button_json["name"].get<std::string>();
    button.position = {
        button_json["position"]["x"].get<float>(),
        button_json["position"]["y"].get<float>(),
        button_json["position"]["z"].get<float>()
    };
    button.size = {
        button_json["size"]["x"].get<float>(),
        button_json["size"]["y"].get<float>()
    };

    button.color.a = button_json["color"]["a"].get<unsigned char>();
    button.color.r = button_json["color"]["r"].get<unsigned char>();
    button.color.g = button_json["color"]["g"].get<unsigned char>();
    button.color.b = button_json["color"]["b"].get<unsigned char>();

    button.pressedColor.a = button_json["pressed color"]["a"].get<unsigned char>();
    button.pressedColor.r = button_json["pressed color"]["r"].get<unsigned char>();
    button.pressedColor.g = button_json["pressed color"]["g"].get<unsigned char>();
    button.pressedColor.b = button_json["pressed color"]["b"].get<unsigned char>();

    button.hoverColor.a = button_json["hover color"]["a"].get<unsigned char>();
    button.hoverColor.r = button_json["hover color"]["r"].get<unsigned char>();
    button.hoverColor.g = button_json["hover color"]["g"].get<unsigned char>();
    button.hoverColor.b = button_json["hover color"]["b"].get<unsigned char>();

    button.disabledButtonColor.a = button_json["disabled color"]["a"].get<unsigned char>();
    button.disabledButtonColor.r = button_json["disabled color"]["r"].get<unsigned char>();
    button.disabledButtonColor.g = button_json["disabled color"]["g"].get<unsigned char>();
    button.disabledButtonColor.b = button_json["disabled color"]["b"].get<unsigned char>();

    button.disabledText.a = button_json["disabled text color"]["a"].get<unsigned char>();
    button.disabledText.r = button_json["disabled text color"]["r"].get<unsigned char>();
    button.disabledText.g = button_json["disabled text color"]["g"].get<unsigned char>();
    button.disabledText.b = button_json["disabled text color"]["b"].get<unsigned char>();

    button.disabledHoverColor.a = button_json["disabled hover color"]["a"].get<unsigned char>();
    button.disabledHoverColor.r = button_json["disabled hover color"]["r"].get<unsigned char>();
    button.disabledHoverColor.g = button_json["disabled hover color"]["g"].get<unsigned char>();
    button.disabledHoverColor.b = button_json["disabled hover color"]["b"].get<unsigned char>();

    button.roundness = button_json["button roundness"].get<float>();
    button.autoResize = button_json["auto resize"].get<bool>();
}



int LoadProject(vector<Entity>& entities_vector, vector<Light>& lights_vector, vector<AdditionalLightInfo>& lights_info_vector) {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cout << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    json json_data;
    infile >> json_data;

    infile.close();

    entities_vector.clear();
    lights_vector.clear();
    lights_info_vector.clear();
    textElements.clear();
    lit_buttons.clear();
    
    try {
        for (const auto& entity_json : json_data) {
            string type = entity_json["type"].get<std::string>();
            if (type == "entity") {
                Entity entity;
                LoadEntity(entity_json, entity);
                entities_vector.emplace_back(entity);
            }
            else if (type == "light") {
                if (entity_json["isChild"].get<bool>() == true) continue;
                Light light;
                AdditionalLightInfo light_info;
                LoadLight(entity_json, light, light_info);
                lights_info_vector.emplace_back(light_info);
                lights_vector.emplace_back(light);
            }
            else if (type == "text") {
                Text textElement;
                LoadText(entity_json, textElement);
                textElements.emplace_back(textElement);
            }
            else if (type == "button") {
                LitButton button;
                LoadButton(entity_json, button);
                lit_buttons.emplace_back(button);
            }
        }
    } catch (const json::type_error& e) {
        std::cout << "JSON type error: " << e.what() << std::endl;
        return 1;
    }

    UpdateLightsBuffer(false, lights_vector);
    
    return 0;
}
