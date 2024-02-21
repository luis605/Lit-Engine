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


/* Material */
void SerializeMaterial(SurfaceMaterial& material, const char* path)
{
    json j;
    j["color"] = material.color;
    // j["diffuse_texture_path"] = material.diffuse_texture_path;
    // j["specular_texture_path"] = material.specular_texture_path;
    // j["normal_texture_path"] = material.normal_texture_path;
    // j["roughness_texture_path"] = material.roughness_texture_path;
    // j["ao_texture_path"] = material.ao_texture_path;
    j["shininess"] = material.shininess;
    j["specular_intensity"] = material.SpecularIntensity;
    j["roughness"] = material.Roughness;
    j["diffuse_intensity"] = material.DiffuseIntensity;

    std::ofstream outfile(path);
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open material file: " << path << std::endl;
        return;
    }

    outfile << std::setw(4) << j;
    outfile.close();
}

void DeserializeMaterial(SurfaceMaterial* material, const char* path) {
    // Attempt to open the JSON file for reading
    if (!material)
        return;
        
    std::ifstream infile(path);
    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open material file for reading: " << path << std::endl;
        return;
    }

    try {
        // Parse the JSON data from the file
        json j;
        infile >> j;


        // Check if the JSON object contains the expected material properties
        if (j.contains("color")) {
            material->color = j["color"];
        }
        // // Clear default values or initialize them if necessary
        // material->diffuse_texture_path.clear();
        // material->specular_texture_path.clear();
        // material->normal_texture_path.clear();
        // material->roughness_texture_path.clear();
        // material->ao_texture_path.clear();
        material->shininess = 0.0f;
        material->SpecularIntensity = 0.0f;
        material->Roughness = 0.0f;
        material->DiffuseIntensity = 0.0f;

        // if (j.contains("diffuse_texture_path")) {
        //     material->diffuse_texture_path = j["diffuse_texture_path"].get<std::string>();
        // }
        // if (j.contains("specular_texture_path")) {
        //     material->specular_texture_path = j["specular_texture_path"].get<std::string>();
        // }
        // if (j.contains("normal_texture_path")) {
        //     material->normal_texture_path = j["normal_texture_path"].get<std::string>();
        // }
        // if (j.contains("roughness_texture_path")) {
        //     material->roughness_texture_path = j["roughness_texture_path"].get<std::string>();
        // }
        // if (j.contains("ao_texture_path")) {
        //     material->ao_texture_path = j["ao_texture_path"].get<std::string>();
        // }
        if (j.contains("shininess")) {
            material->shininess = j["shininess"];
        }
        if (j.contains("specular_intensity")) {
            material->SpecularIntensity = j["specular_intensity"];
        }
        if (j.contains("roughness")) {
            material->Roughness = j["roughness"];
        }
        if (j.contains("diffuse_intensity")) {
            material->DiffuseIntensity = j["diffuse_intensity"];
        }

        // Close the input file
        infile.close();
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse material file (" << path << "): " << e.what() << std::endl;
        return; // Return to avoid using an invalid material
    }
}


/* Objects */
void SaveCamera(json& json_data, const LitCamera camera);
void SaveEntity(json& json_data, const Entity& entity);
void SaveLight(json& json_data, const Light& light, int light_index);
void SaveText(json& json_data, const Text& text, bool emplace_back = true);
void SaveButton(json& json_data, const LitButton& button);

void SaveCamera(json& json_data, LitCamera camera) {
    json j;
    j["type"] = "camera";
    j["position"]["x"] = camera.position.x;
    j["position"]["y"] = camera.position.y;
    j["position"]["z"] = camera.position.z;
    j["target"]["x"] = camera.target.x;
    j["target"]["y"] = camera.target.y;
    j["target"]["z"] = camera.target.z;
    j["up"]["x"] = camera.up.x;
    j["up"]["y"] = camera.up.y;
    j["up"]["z"] = camera.up.z;
    j["fovy"] = camera.fovy;
    j["projection"] = camera.projection;

    json_data.emplace_back(j);
}



void SaveEntity(json& json_data, const Entity& entity) {
    json j;
    j["type"]                    = "entity";
    j["name"]                    = entity.name;
    j["scale"]                   = entity.scale;
    j["position"]                = entity.position;
    j["rotation"]                = entity.rotation;
    j["relative_position"]       = entity.relative_position;
    j["model_path"]              = entity.model_path;
    j["tiling"]                  = entity.tiling;
    
    if (IsModelReady(entity.model) && entity.model_path.empty())
        j["mesh_type"]           = entity.ObjectType;

    j["collider_type"]           = *entity.currentCollisionShapeType;
    j["collider"]                = entity.collider;

    j["script_path"]             = entity.script;
    j["script_index"]            = entity.script_index;
    j["texture_path"]            = entity.texture_path;
    j["normal_texture_path"]     = entity.normal_texture_path;
    j["roughness_texture_path"]  = entity.roughness_texture_path;
    j["material_path"]           = entity.surface_material_path;
    j["id"]                      = entity.id;
    j["surface_material"]        = entity.surface_material;
    j["is_dynamic"]              = entity.isDynamic;
    j["mass"]                    = entity.mass;
    j["lodEnabled"]              = entity.lodEnabled;

    j["mass"]                    = entity.mass;
    j["friction"]                = entity.friction;
    j["damping"]                 = entity.damping;

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
    
    if (light.type == LightType::LIGHT_POINT)
    {
        j["target"]["x"] = 0;
        j["target"]["y"] = 0;
        j["target"]["z"] = 0;
    }
    else
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


void SaveText(json& json_data, const Text& text, bool emplace_back) {
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

void SaveWorldSetting(json& json_data)
{
    json j;
    j["type"] = "world settings";
    // j["gravity"] = gravity;
    j["bloom"] = bloomEnabled;
    j["bloomBrightness"] = bloomBrightness;
    j["bloomSamples"] = bloomSamples;
    
    j["skyboxPath"] = skyboxPath;

    j["ambientColor"] = ambientLight;
    j["skyboxColor"] = skyboxColor;
    json_data.emplace_back(j);
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



std::string serializePythonScript(const std::string &scriptFilePath) {
    std::ifstream inputFile(scriptFilePath);
    if (!inputFile.is_open()) {
        std::cerr << "Failed to open file: " << scriptFilePath << std::endl;
        return "";
    }

    std::string scriptContent((std::istreambuf_iterator<char>(inputFile)),
                              std::istreambuf_iterator<char>());
    inputFile.close();

    return scriptContent;
}

std::map<std::string, std::string> scriptContents;

void serializeScripts() {
    json script_data;

    int file_id = 0;
    for (Entity& entity : entities_list_pregame) {
        if (entity.script.empty()) continue;

        std::string scriptContent = serializePythonScript(entity.script);
        if (scriptContent.empty()) continue;

        json j;
        std::string scriptName = entity.script.substr(entity.script.find_last_of('/') + 1);
        scriptName = scriptName.substr(0, scriptName.find_last_of('.')) + std::to_string(file_id);

        j[scriptName] = scriptContent;
        entity.script_index = scriptName;

        file_id++;
        script_data.emplace_back(j);
    }

    std::ofstream outfile("exported_game/scripts.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return;
    }

    outfile << std::setw(4) << script_data;
    outfile.close();
}


int SaveProject() {
    serializeScripts();
    json json_data;

    SaveCamera(json_data, scene_camera);
    SaveWorldSetting(json_data);

    for (const auto& entity : entities_list_pregame) {
        SaveEntity(json_data, entity);
    }

    int light_index = 0;
    for (const auto& light : lights) {
        if (light.isChild) continue;
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
pair<Light, AdditionalLightInfo> LoadLight(const json& light_json, Light& light, AdditionalLightInfo light_info);



void LoadCamera(const json& camera_json, LitCamera& camera) {
    if (camera_json.contains("position")) {
        Vector3 position{
            camera_json["position"]["x"].get<float>(),
            camera_json["position"]["y"].get<float>(),
            camera_json["position"]["z"].get<float>()
        };
        camera.position = position;
    }

    if (camera_json.contains("target")) {
        Vector3 target{
            camera_json["target"]["x"].get<float>(),
            camera_json["target"]["y"].get<float>(),
            camera_json["target"]["z"].get<float>()
        };
        camera.target = target;
    }

    if (camera_json.contains("up")) {
        Vector3 up{
            camera_json["up"]["x"].get<float>(),
            camera_json["up"]["y"].get<float>(),
            camera_json["up"]["z"].get<float>()
        };
        camera.up = up;
    }

    if (camera_json.contains("fovy")) {
        camera.fovy = camera_json["fovy"].get<float>();
    }

    if (camera_json.contains("projection")) {
        camera.projection = camera_json["projection"].get<int>();
    }
}

void LoadWorldSettings(const json& world_setting_json)
{
    if (world_setting_json.contains("bloom")) {
        bloomEnabled = world_setting_json["bloom"].get<bool>();
    }

    if (world_setting_json.contains("bloomBrightness")) {
        bloomBrightness = world_setting_json["bloomBrightness"].get<float>();
        SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "bloomBrightness"), &bloomBrightness, SHADER_ATTRIB_FLOAT);
    }

    if (world_setting_json.contains("skyboxPath")) {
        InitSkybox(world_setting_json["skyboxPath"].get<std::string>().c_str());
    }

    if (world_setting_json.contains("ambientColor")) {
        ambientLight = world_setting_json["ambientColor"].get<Vector4>();
        SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);
    }

    if (world_setting_json.contains("skyboxColor")) {
        skyboxColor = world_setting_json["skyboxColor"].get<Vector4>();
        SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "skyboxColor"), &skyboxColor, SHADER_UNIFORM_VEC4);
    }
    else
    {
        skyboxColor = (Vector4){1,1,1,1};
        SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "skyboxColor"), &skyboxColor, SHADER_UNIFORM_VEC4);
    }

    if (world_setting_json.contains("bloomSamples")) {
        bloomSamples = world_setting_json["bloomSamples"].get<int>();
        int shaderLocation = glGetUniformLocation(downsamplerShader.id, "samples");

        glUseProgram(downsamplerShader.id);
        glUniform1i(shaderLocation, bloomSamples);
        glUseProgram(0);
    }
}




void LoadEntity(const json& entity_json, Entity& entity) {
    if (entity_json.contains("name")) {
        entity.setName(entity_json["name"].get<std::string>());
    }

    if (entity_json.contains("scale")) {
        Vector3 scale{
            entity_json["scale"]["x"].get<float>(),
            entity_json["scale"]["y"].get<float>(),
            entity_json["scale"]["z"].get<float>()
        };
        entity.setScale(scale);
    }

    if (entity_json.contains("position")) {
        Vector3 position{
            entity_json["position"]["x"].get<float>(),
            entity_json["position"]["y"].get<float>(),
            entity_json["position"]["z"].get<float>()
        };
        entity.position = position;
    }

    if (entity_json.contains("rotation")) {
        Vector3 rotation{
            entity_json["rotation"]["x"].get<float>(),
            entity_json["rotation"]["y"].get<float>(),
            entity_json["rotation"]["z"].get<float>()
        };
        entity.rotation = rotation;
    }

    if (entity_json.contains("relative_position")) {
        Vector3 relative_position{
            entity_json["relative_position"]["x"].get<float>(),
            entity_json["relative_position"]["y"].get<float>(),
            entity_json["relative_position"]["z"].get<float>()
        };
        entity.relative_position = relative_position;
    }

    if (entity_json.contains("mass")) {
        entity.mass = entity_json["mass"].get<float>();
    }

    if (entity_json.contains("friction")) {
        entity.friction = entity_json["friction"].get<float>();
    }

    if (entity_json.contains("damping")) {
        entity.damping = entity_json["damping"].get<float>();
    }

    if (entity_json.contains("lodEnabled")) {
        entity.lodEnabled = entity_json["lodEnabled"].get<bool>();
    }
    
    if (entity_json.contains("tiling")) {
        entity.tiling[0] = entity_json["tiling"][0].get<float>();
        entity.tiling[1] = entity_json["tiling"][1].get<float>();
    }

    if (entity_json.contains("mesh_type")) {
        entity.ObjectType = entity_json["mesh_type"].get<Entity::ObjectTypeEnum>();
    }

    if (entity_json.contains("model_path") && !entity_json["model_path"].get<std::string>().empty()) {
        entity.setModel(
            entity_json["model_path"].get<std::string>().c_str(),
            LoadModel(entity_json["model_path"].get<std::string>().c_str())
        );
    }
    else
    {
        if (entity.ObjectType == Entity::ObjectType_Cube)
            entity.setModel("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));

        else if (entity.ObjectType == Entity::ObjectType_Cone)
            entity.setModel("", LoadModelFromMesh(GenMeshCone(.5, 1, 10)));

        else if (entity.ObjectType == Entity::ObjectType_Cylinder)
            entity.setModel("", LoadModelFromMesh(GenMeshCylinder(.5, 2, 30)));

        else if (entity.ObjectType == Entity::ObjectType_Plane)
            entity.setModel("", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));

        else if (entity.ObjectType == Entity::ObjectType_Sphere)
            entity.setModel("", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));

        else if (entity.ObjectType == Entity::ObjectType_Torus)
            entity.setModel("", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
    }

    if (entity_json.contains("is_dynamic"))
        entity.isDynamic = entity_json["is_dynamic"].get<bool>();

    if (entity_json.contains("mass"))
        entity.mass = entity_json["mass"].get<float>();

    if (entity_json.contains("collider_type"))
        entity.currentCollisionShapeType = make_shared<Entity::CollisionShapeType>(entity_json["collider_type"].get<Entity::CollisionShapeType>());

    if (entity_json.contains("collider"))
        entity.collider = entity_json["collider"].get<bool>();
    if (entity_json.contains("script_path"))
        entity.script = entity_json["script_path"].get<std::string>();
    if (entity_json.contains("script_index"))
        entity.script_index = entity_json["script_index"].get<std::string>();
    if (entity_json.contains("id"))
        entity.id = entity_json["id"].get<int>();
    else
        entity.id = -1;




    // Materials
    if (entity_json.contains("material_path")) {
        entity.surface_material_path = entity_json["material_path"].get<string>();
        if (!entity.surface_material_path.empty())
        {
            DeserializeMaterial(&entity.surface_material, entity.surface_material_path.string().c_str());
        }
    }



    // Textures
    string texture_path = entity_json["texture_path"].get<std::string>();
    if (!texture_path.empty())
    {

        Texture2D diffuse_texture = LoadTexture(texture_path.c_str());
        if (!IsTextureReady(diffuse_texture))
        {            
            entity.texture_path = texture_path;
            entity.texture = std::make_unique<VideoPlayer>(texture_path.c_str());
        }
        else
        {
            entity.texture_path = texture_path;
            entity.texture = diffuse_texture;
        }
    }

    entity.normal_texture_path = entity_json["normal_texture_path"].get<std::string>();
    if (!entity.normal_texture_path.empty())
    {
        entity.normal_texture = LoadTexture(entity.normal_texture_path.string().c_str());
    }

    entity.roughness_texture_path = entity_json["roughness_texture_path"].get<std::string>();
    if (!entity.roughness_texture_path.empty())
    {
        entity.roughness_texture = LoadTexture(entity.roughness_texture_path.string().c_str());
    }



    entity.setShader(shader);

    if (entity_json.contains("children")) {
        const json& children_data = entity_json["children"];
        if (children_data.is_array()) {
            for (const auto& child_array : children_data) {
                if (!child_array.empty()) {
                    const json& child_json = child_array[0];
                    string type = child_json["type"].get<std::string>();
                    if (type == "entity") {
                        Entity* child = new Entity();
                        LoadEntity(child_json, *child);
                        entity.children.push_back(child);
                    } else if (type == "light") {
                        Light* child = new Light();
                        AdditionalLightInfo* light_info = new AdditionalLightInfo();
                        pair<Light, AdditionalLightInfo> light_pair = LoadLight(child_json, *child, *light_info);
                        lights.push_back(light_pair.first);
                        lights_info.push_back(light_pair.second);

                        entity.addChild(&lights.back(), lights_info.back().id);
                    }
                }
            }
        }
    }
}



pair<Light, AdditionalLightInfo> LoadLight(const json& light_json, Light& light, AdditionalLightInfo light_info) {
    light.color = (glm::vec4{
        light_json["color"]["r"].get<float>() / 255,
        light_json["color"]["g"].get<float>() / 255,
        light_json["color"]["b"].get<float>() / 255,
        light_json["color"]["a"].get<float>() / 255
    });
    
    light_info.name = light_json["name"].get<std::string>();

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
    
    light.id      = light_json["id"].get<int>();
    light_info.id = light.id;

    light.intensity          = light_json["intensity"].get<float>();
    light.cutOff             = light_json["cutOff"].get<float>();
    light.specularStrength   = light_json["specularStrength"].get<float>();
    light.attenuation        = light_json["attenuation"].get<float>();
    light.isChild            = light_json["isChild"].get<bool>();
    light.type               = (LightType)light_json["light_type"].get<int>();

    return pair<Light, AdditionalLightInfo>(light, light_info);
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
    LoadText(button_json["text"], button.text);

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



int LoadProject(vector<Entity>& entities_vector, vector<Light>& lights_vector, vector<AdditionalLightInfo>& lights_info_vector, LitCamera& camera) {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cout << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    json json_data;
    infile >> json_data;

    infile.close();

    entities_vector.clear();

#ifndef GAME_SHIPPING
    entities_list_pregame.clear();
#endif

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
                entities_vector.back().reloadRigidBody();
            }
            else if (type == "camera") {
                LoadCamera(entity_json, camera);
            }
            else if (type == "world settings")
            {
                LoadWorldSettings(entity_json);
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
