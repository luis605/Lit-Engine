#include "../include_all.h"

using json = nlohmann::json; // define a shortcut for the nlohmann::json type

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










int SaveProject()
{
   

    // create a JSON object and add each object in the vector to it
    json json_data;
    for (const auto& obj : entities_list_pregame) {
        //json model_data = ModelToString(obj.model);

        json j;
        j["color"] = obj.color;
        j["name"] = obj.name;
        j["scale"] = obj.scale;
        j["position"] = obj.position;
        j["model_path"] = obj.model_path;
        j["script_path"] = obj.script;
        json_data.emplace_back(j);
    }

    ofstream outfile("project.json");
    if (!outfile.is_open()) {
        cerr << "Error: Failed to open project file." << endl;
        return 1;
    }
    outfile << setw(4) << json_data; // indent the JSON for readability

    outfile.close();

    return 0;
}




void LoadProject() {
    ifstream infile("project.json");
    if (!infile.is_open()) {
        cerr << "Error: Failed to open input file." << endl;
        return 1;
    }

    json json_data;
    infile >> json_data; // read the JSON data from the file

    infile.close();


    // iterate over the JSON array and deserialize each object to an Entity
    for (const auto& j : json_data) {
        Entity entity;
        entity.setColor(j["color"]);
        entity.setName(j["name"]);
        entity.setScale(j["scale"]);
        entity.position = j["position"];
        entity.model = LoadModel(string(j["model_path"]).c_str());
        entity.script = string(j["script_path"]);
        


        entities_list_pregame.push_back(entity);
    }
}
