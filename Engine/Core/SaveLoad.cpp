#include "SaveLoad.hpp"
#include <fstream>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <Engine/Core/Events.hpp>

std::map<std::string, std::string> scriptContents;

bool isSubPath(const fs::path &path, const fs::path &base) {
    auto rel = fs::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

void SerializeMaterial(SurfaceMaterial& material, const fs::path path) {
    std::ofstream outfile(path);
    if (!outfile.is_open()) {
        TraceLog(LOG_ERROR, (std::string("Failed to open material file: ") + path.string()).c_str());
        return;
    }

    json j;
    j["albedoTexturePath"]    = material.albedoTexturePath;
    j["normalTexturePath"]    = material.normalTexturePath;
    j["roughnessTexturePath"] = material.roughnessTexturePath;
    j["aoTexturePath"]        = material.aoTexturePath;
    j["heightTexturePath"]    = material.heightTexturePath;
    j["metallicTexturePath"]  = material.metallicTexturePath;
    j["emissiveTexturePath"]      = material.emissiveTexturePath;

    outfile << std::setw(4) << j;
    outfile.close();
}

void DeserializeMaterial(SurfaceMaterial* material, const fs::path& path) {
    if (!material || !fs::exists(path)) return;

    // Path Traversal Vulnerability Prevention
    fs::path resolvedPath = fs::canonical(path);
    const fs::path baseDir = fs::current_path() / "project/";

    if (!isSubPath(resolvedPath, baseDir)) {
        TraceLog(LOG_ERROR, (std::string("Path traversal detected: ") + path.string()).c_str());
        return;
    }

    std::ifstream infile(resolvedPath);

    if (!infile.is_open()) {
        TraceLog(LOG_ERROR, (std::string("Failed to open material file for reading: ") + path.string()).c_str());
        return;
    }

    try {
        json j;
        infile >> j;

        material->albedoTexturePath.clear();
        material->normalTexturePath.clear();
        material->roughnessTexturePath.clear();
        material->aoTexturePath.clear();
        material->heightTexturePath.clear();
        material->metallicTexturePath.clear();
        material->emissiveTexturePath.clear();

        if (j.contains("albedoTexturePath") && !j["albedoTexturePath"].get<std::string>().empty()) {
            material->albedoTexturePath = j["albedoTexturePath"].get<std::string>();

            if (material->albedoTexture.isEmpty())
                material->albedoTexture = material->albedoTexturePath;
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
        if (j.contains("heightTexturePath") && !j["heightTexturePath"].get<std::string>().empty()) {
            material->heightTexturePath = j["heightTexturePath"].get<std::string>();

            if (material->heightTexture.isEmpty())
                material->heightTexture = material->heightTexturePath;
        }
        if (j.contains("metallicTexturePath") && !j["metallicTexturePath"].get<std::string>().empty()) {
            material->metallicTexturePath = j["metallicTexturePath"].get<std::string>();

            if (material->metallicTexture.isEmpty())
                material->metallicTexture = material->metallicTexturePath;
        }
        if (j.contains("emissiveTexturePath") && !j["emissiveTexturePath"].get<std::string>().empty()) {
            material->emissiveTexturePath = j["emissiveTexturePath"].get<std::string>();

            if (material->emissiveTexture.isEmpty())
                material->emissiveTexture = material->emissiveTexturePath;
        }

        infile.close();
    } catch (const json::parse_error& e) {
        TraceLog(LOG_ERROR, (std::string("Failed to parse material file (") + path.string() + std::string("): ") + std::string(e.what())).c_str());
        return;
    }
}

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
    j["relativePosition"]        = entity.relativePosition;
    j["modelPath"]               = entity.modelPath;
    j["tiling"]                  = entity.tiling;

    if (IsModelReady(entity.model) && entity.modelPath.empty())
        j["mesh_type"]           = entity.ObjectType;

    j["collider_type"]           = entity.currentCollisionShapeType;
    j["collider"]                = entity.collider;
    j["script_path"]             = entity.script;
    j["scriptIndex"]             = entity.scriptIndex;
    j["material_path"]           = entity.surfaceMaterialPath;
    j["id"]                      = entity.id;
    j["is_dynamic"]              = entity.isDynamic;
    j["mass"]                    = entity.mass;
    j["lodEnabled"]              = entity.lodEnabled;
    j["mass"]                    = entity.mass;
    j["friction"]                = entity.friction;
    j["damping"]                 = entity.damping;

    json childrenData;

    if (!entity.entitiesChildren.empty()) {
        for (int entityChildIndex : entity.entitiesChildren) {
            json childJson;
            SaveEntity(childJson, *getEntityById(entityChildIndex));
            childrenData.emplace_back(childJson);
        }
    }

    if (!entity.lightsChildren.empty()) {
        for (int lightStructChild : entity.lightsChildren) {
            json childJson;
            SaveLight(childJson, *getLightById(lightStructChild));
            childrenData.emplace_back(childJson);
        }
    }

    j["children"] = childrenData;
    jsonData.emplace_back(j);
}


void SaveLight(json& jsonData, const LightStruct& lightStruct) {
    json j;
    j["type"]       = "light";
    j["name"]       = lightStruct.lightInfo.name;
    j["isChild"]    = lightStruct.isChild;
    j["id"]         = lightStruct.id;
    j["light_type"] = lightStruct.light.type;

    j["position"]   = lightStruct.light.position;

    if (!lightStruct.isChild) {
        j["relativePosition"]["x"] = 0;
        j["relativePosition"]["y"] = 0;
        j["relativePosition"]["z"] = 0;
    } else j["relativePosition"] = lightStruct.lightInfo.relativePosition;

    if (lightStruct.light.type == LIGHT_POINT) {
        j["target"]["x"] = 0;
        j["target"]["y"] = 0;
        j["target"]["z"] = 0;
    } else j["target"] = lightStruct.lightInfo.target;

    j["direction"]  = lightStruct.light.direction;
    j["color"]["r"] = lightStruct.light.color.r * 255;
    j["color"]["g"] = lightStruct.light.color.g * 255;
    j["color"]["b"] = lightStruct.light.color.b * 255;
    j["color"]["a"] = lightStruct.light.color.a * 255;

    j["attenuation"]      = lightStruct.light.aisr.x;
    j["intensity"]        = lightStruct.light.aisr.y;
    j["specularStrength"] = lightStruct.light.aisr.z;
    j["radius"]           = lightStruct.light.aisr.w;

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

    jsonData.emplace_back(emplaceBack ? j : jsonData);
}

void SaveWorldSetting(json& jsonData) {
    json j;
    j["type"] = "world settings";
    j["gravity"] = physics.gravity;
    j["bloom"] = bloomEnabled;
    j["bloomThreshold"] = bloomThreshold;
    j["kernelSize"] = kernelSize;
    j["skyboxPath"] = skybox.skyboxTexturePath;
    j["ambientColor"] = ambientLight;
    j["skyboxColor"] = skybox.color;

    jsonData.emplace_back(j);
}

void SaveButton(json& jsonData, const LitButton& button) {
    json j;
    j["type"] = "button";
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

    json textJson;
    SaveText(textJson, button.text, false);
    j["text"] = textJson;

    jsonData.emplace_back(j);
}

std::string serializePythonScript(const fs::path& path) {
    if (!fs::exists(path)) return "";

    // Path Traversal Vulnerability Prevention
    fs::path resolvedPath = fs::canonical(path);
    const fs::path baseDir = fs::current_path() / "project/";

    if (!isSubPath(resolvedPath, baseDir)) {
        TraceLog(LOG_ERROR, (std::string("Path traversal detected: ") + path.string()).c_str());
        return "";
    }

    std::ifstream inputFile(resolvedPath);
    if (!inputFile.is_open()) {
        TraceLog(LOG_ERROR, (std::string("Failed to open file: " + resolvedPath.string()).c_str()));
        return "";
    }

    std::string scriptContent((std::istreambuf_iterator<char>(inputFile)),
                              std::istreambuf_iterator<char>());
    inputFile.close();

    return scriptContent;
}

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
        TraceLog(LOG_ERROR, "Failed to open exported_game/scripts.json file.");
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

    eventManager.onSceneSave.triggerEvent();

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
        TraceLog(LOG_ERROR, "Failed to open project file.");
        return 1;
    }

    outfile << std::setw(4) << jsonData;
    outfile.close();

    return 0;
}

void LoadCamera(const json& cameraJson, LitCamera& camera) {
    if (cameraJson.contains("position")) {
        camera.position = Vector3{
            cameraJson["position"]["x"].get<float>(),
            cameraJson["position"]["y"].get<float>(),
            cameraJson["position"]["z"].get<float>()
        };
    }

    if (cameraJson.contains("target")) {
        camera.target = Vector3{
            cameraJson["target"]["x"].get<float>(),
            cameraJson["target"]["y"].get<float>(),
            cameraJson["target"]["z"].get<float>()
        };
    }

    if (cameraJson.contains("up")) {
        camera.up = Vector3{
            cameraJson["up"]["x"].get<float>(),
            cameraJson["up"]["y"].get<float>(),
            cameraJson["up"]["z"].get<float>()
        };
    }

    if (cameraJson.contains("fovy")) {
        camera.fovy = cameraJson["fovy"].get<float>();
    }

    if (cameraJson.contains("projection")) {
        camera.projection = cameraJson["projection"].get<int>();
    }
}

void LoadWorldSettings(const json& worldSettingsJson) {
    if (worldSettingsJson.contains("bloom")) {
        bloomEnabled = worldSettingsJson["bloom"].get<bool>();
    }

    if (worldSettingsJson.contains("bloomThreshold")) {
        bloomThreshold = worldSettingsJson["bloomThreshold"].get<float>();
        SetShaderValue(upsamplerShader, GetUniformLocation(upsamplerShader, "threshold"), &bloomThreshold, SHADER_ATTRIB_FLOAT);
    }

    if (worldSettingsJson.contains("skyboxPath")) {
        skybox.loadSkybox(worldSettingsJson["skyboxPath"].get<fs::path>());
    }

    if (worldSettingsJson.contains("ambientColor")) {
        ambientLight = worldSettingsJson["ambientColor"].get<Vector4>();
        SetShaderValue(shader, GetUniformLocation(shader, "ambientLight"), &ambientLight, SHADER_UNIFORM_VEC4);
    }

    if (worldSettingsJson.contains("gravity")) {
        physics.gravity.x = worldSettingsJson["gravity"]["x"].get<float>();
        physics.gravity.y = worldSettingsJson["gravity"]["y"].get<float>();
        physics.gravity.z = worldSettingsJson["gravity"]["z"].get<float>();
    }

    if (worldSettingsJson.contains("skyboxColor"))  skybox.color = worldSettingsJson["skyboxColor"].get<Vector4>();
    else                                            skybox.color = { 1, 1, 1, 1 };
    SetShaderValue(skybox.cubeModel.materials[0].shader, GetUniformLocation(skybox.cubeModel.materials[0].shader, "skyboxColor"), &skybox.color, SHADER_UNIFORM_VEC4);
}

Entity* LoadEntity(const json& entityJson) {
    int id = -1;
    if (entityJson.contains("id"))
        id = entityJson["id"].get<int>();

    Entity* entity = AddEntity("", LoadModelFromMesh(GenMeshCube(1,1,1)), "Unnamed Entity", id);

    if (entityJson.contains("name")) {
        entity->setName(entityJson["name"].get<std::string>());
    }

    if (entityJson.contains("scale")) {
        Vector3 scale{
            entityJson["scale"]["x"].get<float>(),
            entityJson["scale"]["y"].get<float>(),
            entityJson["scale"]["z"].get<float>()
        };
        entity->setScale(scale);
    }

    if (entityJson.contains("position")) {
        entity->position = Vector3{
            entityJson["position"]["x"].get<float>(),
            entityJson["position"]["y"].get<float>(),
            entityJson["position"]["z"].get<float>()
        };
    }

    if (entityJson.contains("rotation")) {
        entity->rotation = Vector3{
            entityJson["rotation"]["x"].get<float>(),
            entityJson["rotation"]["y"].get<float>(),
            entityJson["rotation"]["z"].get<float>()
        };
    }

    if (entityJson.contains("relativePosition")) {
        entity->relativePosition = Vector3{
            entityJson["relativePosition"]["x"].get<float>(),
            entityJson["relativePosition"]["y"].get<float>(),
            entityJson["relativePosition"]["z"].get<float>()
        };
    }

    if (entityJson.contains("mass")) {
        entity->mass = entityJson["mass"].get<float>();
    }

    if (entityJson.contains("friction")) {
        entity->friction = entityJson["friction"].get<float>();
    }

    if (entityJson.contains("damping")) {
        entity->damping = entityJson["damping"].get<float>();
    }

    if (entityJson.contains("lodEnabled")) {
        entity->lodEnabled = entityJson["lodEnabled"].get<bool>();
    }

    if (entityJson.contains("tiling")) {
        entity->tiling[0] = entityJson["tiling"][0].get<float>();
        entity->tiling[1] = entityJson["tiling"][1].get<float>();
    }

    if (entityJson.contains("mesh_type")) {
        entity->ObjectType = entityJson["mesh_type"].get<Entity::ObjectTypeEnum>();
    }

    if (entityJson.contains("modelPath") && !entityJson["modelPath"].get<std::string>().empty()) {
        entity->setModel(
            entityJson["modelPath"].get<std::string>().c_str(),
            LoadModel(entityJson["modelPath"].get<std::string>().c_str())
        );
    } else {
        if (entity->ObjectType == Entity::ObjectType_Cube)           entity->setModel("", LoadModelFromMesh(GenMeshCube(1, 1, 1)));
        else if (entity->ObjectType == Entity::ObjectType_Cone)      entity->setModel("", LoadModelFromMesh(GenMeshCone(.5, 1, 10)));
        else if (entity->ObjectType == Entity::ObjectType_Cylinder)  entity->setModel("", LoadModelFromMesh(GenMeshCylinder(.5, 2, 30)));
        else if (entity->ObjectType == Entity::ObjectType_Plane)     entity->setModel("", LoadModelFromMesh(GenMeshPlane(1, 1, 1, 1)));
        else if (entity->ObjectType == Entity::ObjectType_Sphere)    entity->setModel("", LoadModelFromMesh(GenMeshSphere(.5, 50, 50)));
        else if (entity->ObjectType == Entity::ObjectType_Torus)     entity->setModel("", LoadModelFromMesh(GenMeshTorus(.5, 1, 30, 30)));
    }

    if (entityJson.contains("is_dynamic"))
        entity->isDynamic = entityJson["is_dynamic"].get<bool>();

    if (entityJson.contains("mass"))
        entity->mass = entityJson["mass"].get<float>();

    if (entityJson.contains("collider_type"))
        entity->currentCollisionShapeType = entityJson["collider_type"].get<CollisionShapeType>();

    if (entityJson.contains("collider"))
        entity->collider = entityJson["collider"].get<bool>();

    if (entityJson.contains("script_path"))
        entity->script = entityJson["script_path"].get<std::string>();

    if (entityJson.contains("scriptIndex"))
        entity->scriptIndex = entityJson["scriptIndex"].get<std::string>();

    entity->reloadRigidBody();

    if (entityJson.contains("material_path")) {
        entity->surfaceMaterialPath = entityJson["material_path"].get<std::string>();
        if (!entity->surfaceMaterialPath.empty()) DeserializeMaterial(&entity->surfaceMaterial, entity->surfaceMaterialPath);
    }

    entity->setShader(shader);

    // Deserialize children
    if (entityJson.contains("children")) {
        const json& childrenData = entityJson["children"];
        if (childrenData.is_array()) {
            for (const auto& childArray : childrenData) {
                if (!childArray.empty()) {
                    const json& childJson = childArray[0];
                    std::string type = childJson["type"].get<std::string>();
                    if (type == "entity") {
                        int id = entity->id;
                        Entity* child = LoadEntity(childJson);
                        Entity* reloadedEntity = getEntityById(id); // Reload necessary because entity* gets invalid after entitiesListPregame gets resized!
                        reloadedEntity->addEntityChild(child->id);
                    } else if (type == "light") {
                        LightStruct& lightStruct = LoadLight(childJson);
                        entity->addLightChild(lightStruct.id);
                    }
                }
            }
        }
    }

    return entity;
}

LightStruct& LoadLight(const json& lightJson) {
    // Initialize default values in case some JSON fields are missing or invalid
    int id = lightJson.contains("id") && lightJson["id"].is_number_integer() ? lightJson["id"].get<int>() : 0;

    LightStruct& lightStruct = NewLight({0, 0, 0}, WHITE, LIGHT_POINT, id);

    lightStruct.lightInfo.name = lightJson.contains("name") && lightJson["name"].is_string()
        ? lightJson["name"].get<std::string>()
        : "Unnamed Light";

    lightStruct.isChild = lightJson.contains("isChild") && lightJson["isChild"].is_boolean()
        ? lightJson["isChild"].get<bool>()
        : false;

    lightStruct.light.type = lightJson.contains("light_type") && lightJson["light_type"].is_number_integer()
        ? lightJson["light_type"].get<int>()
        : LIGHT_POINT;

    auto getVec3 = [](const json& j, const std::string& key) -> glm::vec3 {
        if (j.contains(key) && j[key].is_object()) {
            return glm::vec3{
                j[key].contains("x") && j[key]["x"].is_number() ? j[key]["x"].get<float>() : 0.0f,
                j[key].contains("y") && j[key]["y"].is_number() ? j[key]["y"].get<float>() : 0.0f,
                j[key].contains("z") && j[key]["z"].is_number() ? j[key]["z"].get<float>() : 0.0f
            };
        }
        return glm::vec3{0.0f, 0.0f, 0.0f};
    };

    lightStruct.light.position = getVec3(lightJson, "position");
    lightStruct.lightInfo.relativePosition = getVec3(lightJson, "relativePosition");
    lightStruct.lightInfo.target = getVec3(lightJson, "target");
    lightStruct.light.direction = getVec3(lightJson, "direction");

    if (lightJson.contains("color") && lightJson["color"].is_object()) {
        lightStruct.light.color = glm::vec4{
            lightJson["color"].contains("r") && lightJson["color"]["r"].is_number() ? lightJson["color"]["r"].get<float>() / 255 : 1.0f,
            lightJson["color"].contains("g") && lightJson["color"]["g"].is_number() ? lightJson["color"]["g"].get<float>() / 255 : 1.0f,
            lightJson["color"].contains("b") && lightJson["color"]["b"].is_number() ? lightJson["color"]["b"].get<float>() / 255 : 1.0f,
            lightJson["color"].contains("a") && lightJson["color"]["a"].is_number() ? lightJson["color"]["a"].get<float>() / 255 : 1.0f
        };
    } else {
        lightStruct.light.color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f};
    }

    auto getFloat = [](const json& j, const std::string& key, float defaultValue) -> float {
        return j.contains(key) && j[key].is_number() ? j[key].get<float>() : defaultValue;
    };

    lightStruct.light.aisr.x = getFloat(lightJson, "attenuation", 1.0f);
    lightStruct.light.aisr.y = getFloat(lightJson, "intensity", 1.0f);
    lightStruct.light.aisr.z = getFloat(lightJson, "specularStrength", 0.5f);
    lightStruct.light.aisr.w = getFloat(lightJson, "radius", 10.0f);

    return lightStruct;
}

void LoadText(const json& textJson, Text& text) {
    text.name                = textJson["name"].get<std::string>();
    text.text                = textJson["text"].get<std::string>();

    text.color.r             = textJson["color"]["r"].get<unsigned char>();
    text.color.g             = textJson["color"]["g"].get<unsigned char>();
    text.color.b             = textJson["color"]["b"].get<unsigned char>();
    text.color.a             = textJson["color"]["a"].get<unsigned char>();

    text.backgroundColor.r             = textJson["background color"]["r"].get<unsigned char>();
    text.backgroundColor.g             = textJson["background color"]["g"].get<unsigned char>();
    text.backgroundColor.b             = textJson["background color"]["b"].get<unsigned char>();
    text.backgroundColor.a             = textJson["background color"]["a"].get<unsigned char>();

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

    button.color.r = buttonJson["color"]["r"].get<unsigned char>();
    button.color.g = buttonJson["color"]["g"].get<unsigned char>();
    button.color.b = buttonJson["color"]["b"].get<unsigned char>();
    button.color.a = buttonJson["color"]["a"].get<unsigned char>();

    button.pressedColor.r = buttonJson["pressed color"]["r"].get<unsigned char>();
    button.pressedColor.g = buttonJson["pressed color"]["g"].get<unsigned char>();
    button.pressedColor.b = buttonJson["pressed color"]["b"].get<unsigned char>();
    button.pressedColor.a = buttonJson["pressed color"]["a"].get<unsigned char>();

    button.hoverColor.r = buttonJson["hover color"]["r"].get<unsigned char>();
    button.hoverColor.g = buttonJson["hover color"]["g"].get<unsigned char>();
    button.hoverColor.b = buttonJson["hover color"]["b"].get<unsigned char>();
    button.hoverColor.a = buttonJson["hover color"]["a"].get<unsigned char>();

    button.disabledButtonColor.r = buttonJson["disabled color"]["r"].get<unsigned char>();
    button.disabledButtonColor.g = buttonJson["disabled color"]["g"].get<unsigned char>();
    button.disabledButtonColor.b = buttonJson["disabled color"]["b"].get<unsigned char>();
    button.disabledButtonColor.a = buttonJson["disabled color"]["a"].get<unsigned char>();

    button.disabledText.r = buttonJson["disabled text color"]["r"].get<unsigned char>();
    button.disabledText.g = buttonJson["disabled text color"]["g"].get<unsigned char>();
    button.disabledText.b = buttonJson["disabled text color"]["b"].get<unsigned char>();
    button.disabledText.a = buttonJson["disabled text color"]["a"].get<unsigned char>();

    button.disabledHoverColor.r = buttonJson["disabled hover color"]["r"].get<unsigned char>();
    button.disabledHoverColor.g = buttonJson["disabled hover color"]["g"].get<unsigned char>();
    button.disabledHoverColor.b = buttonJson["disabled hover color"]["b"].get<unsigned char>();
    button.disabledHoverColor.a = buttonJson["disabled hover color"]["a"].get<unsigned char>();

    button.roundness = buttonJson["button roundness"].get<float>();
    button.autoResize = buttonJson["auto resize"].get<bool>();
}

int LoadProject(std::vector<Entity>& entitiesVector, std::vector<LightStruct>& lightsVector, LitCamera& camera) {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open project file.");
        return 1;
    }

    json jsonData;
    infile >> jsonData;

    infile.close();

    if (!entitiesVector.empty()) entitiesVector.clear();
    if (!lightsVector.empty())   lightsVector.clear();
    if (!textElements.empty())   textElements.clear();
    if (!litButtons.empty())     litButtons.clear();

    eventManager.onSceneLoad.triggerEvent();

    try {
        for (const auto& objectJson : jsonData) {
            std::string type = objectJson["type"].get<std::string>();
            if (type == "entity") {
                LoadEntity(objectJson);
            } else if (type == "camera") {
                LoadCamera(objectJson, camera);
            } else if (type == "world settings") {
                LoadWorldSettings(objectJson);
            } else if (type == "light") {
                if (objectJson["isChild"].get<bool>() == true) continue;
                LoadLight(objectJson);
            } else if (type == "text") {
                Text textElement;
                LoadText(objectJson, textElement);
                textElements.emplace_back(std::move(textElement));
            } else if (type == "button") {
                LitButton button;
                LoadButton(objectJson, button);
                litButtons.emplace_back(std::move(button));
            }
        }
    } catch (const json::type_error& e) {
        TraceLog(LOG_ERROR, (std::string("JSON type error: ") + std::string(e.what())).c_str());
        return 1;
    }

    UpdateLightsBuffer(false, lightsVector);

    return 0;
}