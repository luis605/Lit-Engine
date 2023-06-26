#include "../include_all.h"

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
}

namespace nlohmann {
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
}

void SaveEntity(json& json_data, const Entity& entity) {
    json j;
    j["color"] = entity.color;
    j["name"] = entity.name;
    j["scale"] = entity.scale;
    j["position"] = entity.position;
    j["relative_position"] = entity.relative_position;
    j["model_path"] = entity.model_path;
    j["script_path"] = entity.script;
    j["id"] = entity.id;

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

int SaveProject() {
    json json_data;
    for (const auto& entity : entities_list_pregame) {
        SaveEntity(json_data, entity);
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
    entity.id = entity_json["id"].get<std::string>();
    
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


int LoadProject() {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return 1;
    }

    json json_data;
    infile >> json_data;

    infile.close();

    entities_list_pregame.clear();

    try {
        for (const auto& entity_json : json_data) {
            Entity entity;
            LoadEntity(entity_json, entity);
            entities_list_pregame.push_back(entity);
        }
    } catch (const json::type_error& e) {
        std::cerr << "JSON type error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
