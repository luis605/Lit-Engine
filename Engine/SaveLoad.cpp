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





json MeshToJson(const Mesh& mesh) {
    json result;

    // Serialize mesh data to result
    result["vertices"] = nlohmann::json::array();
    
    int num_vertices = mesh.vertexCount;

    for (int i = 0; i < num_vertices; ++i) {
        result["vertices"].push_back(mesh.vertices[i]);
    }

    return result;
}


json MaterialToJson(const Material& material) {
    json result;


    return result;
}


json ModelToString(const Model model) {

    // Serialize the model mesh and materials to a JSON object
    json meshes_json;
    for (int index; index<model.meshCount; index++) {
        Mesh mesh = model.meshes[index];
        meshes_json.push_back(MeshToJson(mesh));
    }

    json materials_json;
    for (int index; index<model.materialCount; index++) {
        Material material = model.materials[index];
        materials_json.push_back(MaterialToJson(material));
    }

    // Combine the serialized data into a single JSON object
    json result;
    result = {
        {"meshes", meshes_json},
        {"materials", materials_json}
    };

    return result;
}






int SaveProject()
{
   

    // create a JSON object and add each object in the vector to it
    json json_data;
    for (const auto& obj : entities_list_pregame) {
        json model_data = ModelToString(obj.model);

        json j;
        j["color"] = obj.color;
        j["name"] = obj.name;
        j["scale"] = obj.scale;
        j["position"] = obj.position;
        j["model"] = model_data;
        json_data.emplace_back(j);
    }

    std::ofstream outfile("project.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open project file." << std::endl;
        return 1;
    }
    outfile << std::setw(4) << json_data; // indent the JSON for readability

    outfile.close();

    return 0;
}




void LoadProject() {
    std::ifstream infile("project.json");
    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open input file." << std::endl;
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

        json model_data = j["model"];
        Model model;
        // deserialize the meshes data
        for (const auto& m : model_data["meshes"]) {
            Mesh mesh;
            // deserialize the vertices data


            std::vector<float> vertices;
            for (const auto& v : m["vertices"]) {
                vertices.push_back(v);
            }
            mesh.vertices = vertices.data(); // set the pointer to the start of the array
            mesh.vertexCount = vertices.size(); // set the count of vertices
            
            model.meshes = &mesh;
        }
        // deserialize the materials data
        for (const auto& mat : model_data["materials"]) {
            Material material;
            // deserialize the material data
            // (assuming there is a MaterialToJson() function that serializes a Material object to JSON)
            // ...
            model.materials = &material;
        }
        entity.model = LoadModel("assets/models/tree.obj");

        entities_list_pregame.push_back(entity);
    }
}
