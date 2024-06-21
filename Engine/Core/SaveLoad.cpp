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

bool is_subpath(const std::filesystem::path &path,
                const std::filesystem::path &base)
{
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

/* Material */
void SerializeMaterial(SurfaceMaterial& material, const fs::path path) {
    json j;
    j["color"] = material.color;
    j["diffuseTexturePath"] = material.diffuseTexturePath;
    j["normalTexturePath"] = material.normalTexturePath;
    j["roughnessTexturePath"] = material.roughnessTexturePath;
    j["aoTexturePath"] = material.aoTexturePath;
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

void DeserializeMaterial(SurfaceMaterial* material, const fs::path path) {
    if (!material)
        return;

    // Path Traversal Vulnerability Prevention
    fs::path resolvedPath = fs::canonical(path);
    const fs::path baseDir = fs::current_path() / "project/";

    if (!is_subpath(resolvedPath, baseDir)) {
        std::cerr << "Error: Path traversal detected: " << path << std::endl;
        return;
    }

    std::ifstream infile(resolvedPath);

    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open material file for reading: " << path << std::endl;
        return;
    }

    try {
        json j;
        infile >> j;

        if (j.contains("color")) {
            material->color = j["color"];
        }

        material->diffuseTexturePath.clear();
        material->normalTexturePath.clear();
        material->roughnessTexturePath.clear();
        material->aoTexturePath.clear();
        material->shininess = 0.0f;
        material->SpecularIntensity = 0.0f;
        material->Roughness = 0.0f;
        material->DiffuseIntensity = 0.0f;

        if (j.contains("diffuseTexturePath") && !j["diffuseTexturePath"].get<std::string>().empty()) {
            material->diffuseTexturePath = j["diffuseTexturePath"].get<std::string>();

            if (material->diffuseTexture.isEmpty())
                material->diffuseTexture = material->diffuseTexturePath;
        }
        if (j.contains("normalTexturePath") && !j["normalTexturePath"].get<std::string>().empty()) {
            material->normalTexturePath = j["normalTexturePath"].get<std::string>();

            if (material->normalTexture.isEmpty())
                material->normalTexture = material->normalTexturePath;
        }
        if (j.contains("roughnessTexturePath") && !j["roughnessTexturePath"].get<std::string>().empty()) {
            material->roughnessTexturePath = j["roughnessTexturePath"].get<std::string>();

            if (material->roughnessTexture.isEmpty())
                material->roughnessTexture = material->roughnessTexturePath;
        }
        if (j.contains("aoTexturePath") && !j["aoTexturePath"].get<std::string>().empty()) {
            material->aoTexturePath = j["aoTexturePath"].get<std::string>();

            if (material->aoTexture.isEmpty())
                material->aoTexture = material->aoTexturePath;
        }
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
void SaveCamera(json& jsonData, const LitCamera camera);
void SaveEntity(json& jsonData, const Entity& entity);
void SaveLight(json& jsonData, const LightStruct& lightStruct);
void SaveText(json& jsonData, const Text& text, bool emplaceBack = true);
void SaveButton(json& jsonData, const LitButton& button);

void SaveCamera(json& jsonData, LitCamera camera) {
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

    jsonData.emplace_back(j);
}



void SaveEntity(json& jsonData, const Entity& entity) {
    json j;
    j["type"]                    = "entity";
    j["name"]                    = entity.name;
    j["scale"]                   = entity.scale;
    j["position"]                = entity.position;
    j["rotation"]                = entity.rotation;
    j["relativePosition"]       = entity.relativePosition;
    j["modelPath"]              = entity.modelPath;
    j["tiling"]                  = entity.tiling;
    
    if (IsModelReady(entity.model) && entity.modelPath.empty())
        j["mesh_type"]           = entity.ObjectType;

    j["collider_type"]           = *entity.currentCollisionShapeType;
    j["collider"]                = entity.collider;
    j["script_path"]             = entity.script;
    j["scriptIndex"]            = entity.scriptIndex;
    j["material_path"]           = entity.surfaceMaterialPath;
    j["surfaceMaterial"]        = entity.surfaceMaterial;
    j["id"]                      = entity.id;
    j["is_dynamic"]              = entity.isDynamic;
    j["mass"]                    = entity.mass;
    j["lodEnabled"]              = entity.lodEnabled;

    j["mass"]                    = entity.mass;
    j["friction"]                = entity.friction;
    j["damping"]                 = entity.damping;

    json childrenData;

    if (!entity.entitiesChildren.empty()) {
        for (Entity* entityChild : entity.entitiesChildren) {
            json childJson;
            SaveEntity(childJson, *entityChild);
            childrenData.emplace_back(childJson);
        }
    }

    if (!entity.lightsChildren.empty()) {
        for (LightStruct* lightStructChild : entity.lightsChildren) {
            json childJson;
            SaveLight(childJson, *lightStructChild);
            childrenData.emplace_back(childJson);
        }
    }
    j["children"] = childrenData;

    jsonData.emplace_back(j);
}


void SaveLight(json& jsonData, const LightStruct& lightStruct) {
    json j;
    j["type"] = "light";
    j["color"]["r"] = lightStruct.light.color.r * 255;
    j["color"]["g"] = lightStruct.light.color.g * 255;
    j["color"]["b"] = lightStruct.light.color.b * 255;
    j["color"]["a"] = lightStruct.light.color.a * 255;
    
    j["name"] = lightStruct.lightInfo.name;
    j["position"] = lightStruct.light.position;
    
    if (lightStruct.isChild) {
        j["relativePosition"] = lightStruct.light.relativePosition;
    }else {
        j["relativePosition"]["x"] = 0;
        j["relativePosition"]["y"] = 0;
        j["relativePosition"]["z"] = 0;
    }
    
    if (lightStruct.light.type == LIGHT_POINT) {
        j["target"]["x"] = 0;
        j["target"]["y"] = 0;
        j["target"]["z"] = 0;
    } else j["target"] = lightStruct.light.target;

    j["direction"] = lightStruct.light.direction;
    j["intensity"] = lightStruct.light.intensity;
    j["cutOff"] = lightStruct.light.cutOff;
    j["specularStrength"] = lightStruct.light.specularStrength;
    j["attenuation"] = lightStruct.light.attenuation;
    j["isChild"] = lightStruct.isChild;
    j["id"] = lightStruct.id;
    j["light_type"] = lightStruct.light.type;

    jsonData.emplace_back(j);
}


void SaveText(json& jsonData, const Text& text, bool emplaceBack) {
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

    if (emplaceBack)
        jsonData.emplace_back(j);
    else
       jsonData = j;

}

void SaveWorldSetting(json& jsonData) {
    json j;
    j["type"] = "world settings";
    // j["gravity"] = gravity;
    j["bloom"] = bloomEnabled;
    j["bloomBrightness"] = bloomBrightness;
    j["bloomSamples"] = bloomSamples;
    
    j["skyboxPath"] = skyboxPath;

    j["ambientColor"] = ambientLight;
    j["skyboxColor"] = skyboxColor;
    jsonData.emplace_back(j);
}

void SaveButton(json& jsonData, const LitButton& button) {
    json j;
    j["type"] = "button";
    
    // Create a JSON object for the "text" field
    json textJson;
    SaveText(textJson, button.text, false);
    j["text"] = textJson;

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

    jsonData.emplace_back(j);
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
    json scriptData;

    int fileID = 0;
    for (Entity& entity : entitiesListPregame) {
        if (entity.script.empty()) continue;

        std::string scriptContent = serializePythonScript(entity.script);
        if (scriptContent.empty()) continue;

        json j;
        std::string scriptName = entity.script.substr(entity.script.find_last_of('/') + 1);
        scriptName = scriptName.substr(0, scriptName.find_last_of('.')) + std::to_string(fileID);

        j[scriptName] = scriptContent;
        entity.scriptIndex = scriptName;

        fileID++;
        scriptData.emplace_back(j);
    }

    std::ofstream outfile("exported_game/scripts.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return;
    }

    outfile << std::setw(4) << scriptData;
    outfile.close();
}


int SaveProject() {
    serializeScripts();
    json jsonData;

    SaveCamera(jsonData, sceneCamera);
    SaveWorldSetting(jsonData);

    for (const auto& entity : entitiesListPregame) {
        if (entity.isChild) continue;
        SaveEntity(jsonData, entity);
    }

    for (const auto& lightStruct : lights) {
        if (lightStruct.isChild) continue;
        SaveLight(jsonData, lightStruct);
    }

    for (const Text& text : textElements) {
        SaveText(jsonData, text);
    }

    for (const LitButton& button : litButtons) {
        SaveButton(jsonData, button);
    }

    std::ofstream outfile("project.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    outfile << std::setw(4) << jsonData;
    outfile.close();

    return 0;
}

void LoadEntity(const json& entityJson, Entity& entity, std::vector<Entity>& entitiesVector);
void LoadLight(const json& lightJson, LightStruct& lightStruct);

void LoadCamera(const json& cameraJson, LitCamera& camera) {
    if (cameraJson.contains("position")) {
        Vector3 position{
            cameraJson["position"]["x"].get<float>(),
            cameraJson["position"]["y"].get<float>(),
            cameraJson["position"]["z"].get<float>()
        };
        camera.position = position;
    }

    if (cameraJson.contains("target")) {
        Vector3 target{
            cameraJson["target"]["x"].get<float>(),
            cameraJson["target"]["y"].get<float>(),
            cameraJson["target"]["z"].get<float>()
        };
        camera.target = target;
    }

    if (cameraJson.contains("up")) {
        Vector3 up{
            cameraJson["up"]["x"].get<float>(),
            cameraJson["up"]["y"].get<float>(),
            cameraJson["up"]["z"].get<float>()
        };
        camera.up = up;
    }

    if (cameraJson.contains("fovy")) {
        camera.fovy = cameraJson["fovy"].get<float>();
    }

    if (cameraJson.contains("projection")) {
        camera.projection = cameraJson["projection"].get<int>();
    }
}

void LoadWorldSettings(const json& worldSettingsJson)
{
    if (worldSettingsJson.contains("bloom")) {
        bloomEnabled = worldSettingsJson["bloom"].get<bool>();
    }

    if (worldSettingsJson.contains("bloomBrightness")) {
        bloomBrightness = worldSettingsJson["bloomBrightness"].get<float>();
        SetShaderValue(downsamplerShader, GetShaderLocation(downsamplerShader, "bloomBrightness"), &bloomBrightness, SHADER_ATTRIB_FLOAT);
    }

    if (worldSettingsJson.contains("skyboxPath")) {
        InitSkybox(worldSettingsJson["skyboxPath"].get<std::string>().c_str());
    }

    if (worldSettingsJson.contains("ambientColor")) {
        ambientLight = worldSettingsJson["ambientColor"].get<Vector4>();
        SetShaderValue(shader, GetShaderLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);
    }

    if (worldSettingsJson.contains("skyboxColor")) {
        skyboxColor = worldSettingsJson["skyboxColor"].get<Vector4>();
        SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "skyboxColor"), &skyboxColor, SHADER_UNIFORM_VEC4);
    }
    else
    {
        skyboxColor = (Vector4){1,1,1,1};
        SetShaderValue(skybox.materials[0].shader, GetShaderLocation(skybox.materials[0].shader, "skyboxColor"), &skyboxColor, SHADER_UNIFORM_VEC4);
    }

    if (worldSettingsJson.contains("bloomSamples")) {
        bloomSamples = worldSettingsJson["bloomSamples"].get<int>();
        int shaderLocation = glGetUniformLocation(downsamplerShader.id, "samples");

        glUseProgram(downsamplerShader.id);
        glUniform1i(shaderLocation, bloomSamples);
        glUseProgram(0);
    }
}




void LoadEntity(const json& entityJson, Entity& entity, std::vector<Entity>& entitiesVector) {
    if (entityJson.contains("name")) {
        entity.setName(entityJson["name"].get<std::string>());
    }

    if (entityJson.contains("scale")) {
        Vector3 scale{
            entityJson["scale"]["x"].get<float>(),
            entityJson["scale"]["y"].get<float>(),
            entityJson["scale"]["z"].get<float>()
        };
        entity.setScale(scale);
    }

    if (entityJson.contains("position")) {
        Vector3 position{
            entityJson["position"]["x"].get<float>(),
            entityJson["position"]["y"].get<float>(),
            entityJson["position"]["z"].get<float>()
        };
        entity.position = position;
    }

    if (entityJson.contains("rotation")) {
        Vector3 rotation{
            entityJson["rotation"]["x"].get<float>(),
            entityJson["rotation"]["y"].get<float>(),
            entityJson["rotation"]["z"].get<float>()
        };
        entity.rotation = rotation;
    }

    if (entityJson.contains("relativePosition")) {
        Vector3 relativePosition{
            entityJson["relativePosition"]["x"].get<float>(),
            entityJson["relativePosition"]["y"].get<float>(),
            entityJson["relativePosition"]["z"].get<float>()
        };
        entity.relativePosition = relativePosition;
    }

    if (entityJson.contains("mass")) {
        entity.mass = entityJson["mass"].get<float>();
    }

    if (entityJson.contains("friction")) {
        entity.friction = entityJson["friction"].get<float>();
    }

    if (entityJson.contains("damping")) {
        entity.damping = entityJson["damping"].get<float>();
    }

    if (entityJson.contains("lodEnabled")) {
        entity.lodEnabled = entityJson["lodEnabled"].get<bool>();
    }
    
    if (entityJson.contains("tiling")) {
        entity.tiling[0] = entityJson["tiling"][0].get<float>();
        entity.tiling[1] = entityJson["tiling"][1].get<float>();
    }

    if (entityJson.contains("mesh_type")) {
        entity.ObjectType = entityJson["mesh_type"].get<Entity::ObjectTypeEnum>();
    }

    if (entityJson.contains("modelPath") && !entityJson["modelPath"].get<std::string>().empty()) {
        entity.setModel(
            entityJson["modelPath"].get<std::string>().c_str(),
            LoadModel(entityJson["modelPath"].get<std::string>().c_str())
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

    if (entityJson.contains("is_dynamic"))
        entity.isDynamic = entityJson["is_dynamic"].get<bool>();

    if (entityJson.contains("mass"))
        entity.mass = entityJson["mass"].get<float>();

    if (entityJson.contains("collider_type"))
        entity.currentCollisionShapeType = std::make_shared<Entity::CollisionShapeType>(entityJson["collider_type"].get<Entity::CollisionShapeType>());

    if (entityJson.contains("collider"))
        entity.collider = entityJson["collider"].get<bool>();
    if (entityJson.contains("script_path"))
        entity.script = entityJson["script_path"].get<std::string>();
    if (entityJson.contains("scriptIndex"))
        entity.scriptIndex = entityJson["scriptIndex"].get<std::string>();
    if (entityJson.contains("id"))
        entity.id = entityJson["id"].get<int>();
    else
        entity.id = -1;


    // Materials
    if (entityJson.contains("material_path")) {
        entity.surfaceMaterialPath = entityJson["material_path"].get<std::string>();
        if (!entity.surfaceMaterialPath.empty()) DeserializeMaterial(&entity.surfaceMaterial, entity.surfaceMaterialPath);
    }

    entity.setShader(shader);

    if (entityJson.contains("children")) {
        const json& childrenData = entityJson["children"];
        if (childrenData.is_array()) {
            for (const auto& childArray : childrenData) {
                if (!childArray.empty()) {
                    const json& childJson = childArray[0];
                    std::string type = childJson["type"].get<std::string>();
                    if (type == "entity") {
                        Entity child;
                        LoadEntity(childJson, child, entitiesVector);
                        child.parent = &entity;
                        child.isChild = true;

                        entitiesVector.emplace_back(std::move(child));
                        entitiesVector.back().reloadRigidBody();

                        entity.addChild(&entitiesVector.back());
                    } else if (type == "light") {
                        lights.emplace_back();

                        LightStruct& lightStruct = lights.back();
                        LoadLight(childJson, lightStruct);

                        lightStruct.parent = &entity;
                        entity.addChild(&lightStruct);
                    }
                }
            }
        }
    }
}



void LoadLight(const json& lightJson, LightStruct& lightStruct) {
    lightStruct.light.color = (glm::vec4{
        lightJson["color"]["r"].get<float>() / 255,
        lightJson["color"]["g"].get<float>() / 255,
        lightJson["color"]["b"].get<float>() / 255,
        lightJson["color"]["a"].get<float>() / 255
    });
    
    lightStruct.lightInfo.name = lightJson["name"].get<std::string>();

    lightStruct.light.position = glm::vec3{
        lightJson["position"]["x"].get<float>(),
        lightJson["position"]["y"].get<float>(),
        lightJson["position"]["z"].get<float>()
    };

    lightStruct.light.relativePosition = glm::vec3{
        lightJson["relativePosition"]["x"].get<float>(),
        lightJson["relativePosition"]["y"].get<float>(),
        lightJson["relativePosition"]["z"].get<float>()
    };

    lightStruct.light.target = glm::vec3{
        lightJson["target"]["x"].get<float>(),
        lightJson["target"]["y"].get<float>(),
        lightJson["target"]["z"].get<float>()
    };

    
    lightStruct.light.direction = glm::vec3{
        lightJson["direction"]["x"].get<float>(),
        lightJson["direction"]["y"].get<float>(),
        lightJson["direction"]["z"].get<float>()
    };

    lightStruct.id      = lightJson["id"].get<int>();
    
    lightStruct.light.intensity          = lightJson["intensity"].get<float>();
    lightStruct.light.cutOff             = lightJson["cutOff"].get<float>();
    lightStruct.light.specularStrength   = lightJson["specularStrength"].get<float>();
    lightStruct.light.attenuation        = lightJson["attenuation"].get<float>();
    lightStruct.isChild                  = lightJson["isChild"].get<bool>();
    lightStruct.light.type               = lightJson["light_type"].get<int>();
}


void LoadText(const json& textJson, Text& text) {
    text.name                = textJson["name"].get<std::string>();
    text.text                = textJson["text"].get<std::string>();

    text.color.a             = textJson["color"]["a"].get<unsigned char>();
    text.color.r             = textJson["color"]["r"].get<unsigned char>();
    text.color.g             = textJson["color"]["g"].get<unsigned char>();
    text.color.b             = textJson["color"]["b"].get<unsigned char>();

    text.backgroundColor.a             = textJson["background color"]["a"].get<unsigned char>();
    text.backgroundColor.r             = textJson["background color"]["r"].get<unsigned char>();
    text.backgroundColor.g             = textJson["background color"]["g"].get<unsigned char>();
    text.backgroundColor.b             = textJson["background color"]["b"].get<unsigned char>();

    text.backgroundRoundness          = textJson["background roundiness"].get<float>();
    text.fontSize                      = textJson["font size"].get<float>();
    text.spacing                       = textJson["spacing"].get<float>();
    text.padding                       = textJson["padding"].get<float>();

    text.position = {
        textJson["position"]["x"].get<float>(),
        textJson["position"]["y"].get<float>(),
        textJson["position"]["z"].get<float>()
    };
}


void LoadButton(const json& buttonJson, LitButton& button) {
    LoadText(buttonJson["text"], button.text);

    button.name = buttonJson["name"].get<std::string>();
    button.position = {
        buttonJson["position"]["x"].get<float>(),
        buttonJson["position"]["y"].get<float>(),
        buttonJson["position"]["z"].get<float>()
    };
    button.size = {
        buttonJson["size"]["x"].get<float>(),
        buttonJson["size"]["y"].get<float>()
    };

    button.color.a = buttonJson["color"]["a"].get<unsigned char>();
    button.color.r = buttonJson["color"]["r"].get<unsigned char>();
    button.color.g = buttonJson["color"]["g"].get<unsigned char>();
    button.color.b = buttonJson["color"]["b"].get<unsigned char>();

    button.pressedColor.a = buttonJson["pressed color"]["a"].get<unsigned char>();
    button.pressedColor.r = buttonJson["pressed color"]["r"].get<unsigned char>();
    button.pressedColor.g = buttonJson["pressed color"]["g"].get<unsigned char>();
    button.pressedColor.b = buttonJson["pressed color"]["b"].get<unsigned char>();

    button.hoverColor.a = buttonJson["hover color"]["a"].get<unsigned char>();
    button.hoverColor.r = buttonJson["hover color"]["r"].get<unsigned char>();
    button.hoverColor.g = buttonJson["hover color"]["g"].get<unsigned char>();
    button.hoverColor.b = buttonJson["hover color"]["b"].get<unsigned char>();

    button.disabledButtonColor.a = buttonJson["disabled color"]["a"].get<unsigned char>();
    button.disabledButtonColor.r = buttonJson["disabled color"]["r"].get<unsigned char>();
    button.disabledButtonColor.g = buttonJson["disabled color"]["g"].get<unsigned char>();
    button.disabledButtonColor.b = buttonJson["disabled color"]["b"].get<unsigned char>();

    button.disabledText.a = buttonJson["disabled text color"]["a"].get<unsigned char>();
    button.disabledText.r = buttonJson["disabled text color"]["r"].get<unsigned char>();
    button.disabledText.g = buttonJson["disabled text color"]["g"].get<unsigned char>();
    button.disabledText.b = buttonJson["disabled text color"]["b"].get<unsigned char>();

    button.disabledHoverColor.a = buttonJson["disabled hover color"]["a"].get<unsigned char>();
    button.disabledHoverColor.r = buttonJson["disabled hover color"]["r"].get<unsigned char>();
    button.disabledHoverColor.g = buttonJson["disabled hover color"]["g"].get<unsigned char>();
    button.disabledHoverColor.b = buttonJson["disabled hover color"]["b"].get<unsigned char>();

    button.roundness = buttonJson["button roundness"].get<float>();
    button.autoResize = buttonJson["auto resize"].get<bool>();
}



int LoadProject(std::vector<Entity>& entitiesVector, std::vector<LightStruct>& lightsVector, LitCamera& camera) {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cout << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    json jsonData;
    infile >> jsonData;

    infile.close();

    entitiesVector.clear();

#ifndef GAME_SHIPPING
    entitiesListPregame.clear();
#endif

    lightsVector.clear();
    textElements.clear();
    litButtons.clear();
    
    try {
        for (const auto& objectJson : jsonData) {
            std::string type = objectJson["type"].get<std::string>();
            if (type == "entity") {
                Entity entity;
                LoadEntity(objectJson, entity, entitiesVector);
                entitiesVector.emplace_back(std::move(entity));
                entitiesVector.back().reloadRigidBody();
            }
            else if (type == "camera") {
                LoadCamera(objectJson, camera);
            }
            else if (type == "world settings")
            {
                LoadWorldSettings(objectJson);
            }
            else if (type == "light") {
                if (objectJson["isChild"].get<bool>() == true) continue;
                LightStruct lightStruct;
                LoadLight(objectJson, lightStruct);
                lights.emplace_back(std::move(lightStruct));
            }
            else if (type == "text") {
                Text textElement;
                LoadText(objectJson, textElement);
                textElements.emplace_back(std::move(textElement));
            }
            else if (type == "button") {
                LitButton button;
                LoadButton(objectJson, button);
                litButtons.emplace_back(std::move(button));
            }
        }
    } catch (const json::type_error& e) {
        std::cout << "JSON type error: " << e.what() << std::endl;
        return 1;
    }

    UpdateLightsBuffer(false, lightsVector);
    
    return 0;
}
