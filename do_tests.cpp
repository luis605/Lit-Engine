#include <iostream>
#include <fstream>
#include <vector>
#include "json.hpp" // include the nlohmann/json library
#include "include_all.h"

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






int save() {

    InitWindow(1000, 1000, "Lit Engine");

    Entity entity_create;
    entity_create.setColor(RED);
    entity_create.setScale(Vector3 { 2, 2, 2, });
    entity_create.setName("name");
    entity_create.loadModel("assets/models/gizmo/cube.obj");
    entities_list_pregame.push_back(entity_create);


    entities_list_pregame.push_back(entity_create);



    std::cout << "PASS" << std::endl; 
   
    json model_data = ModelToString(entity_create.model);


    // create a JSON object and add each object in the vector to it
    json json_data;
    for (const auto& obj : entities_list_pregame) {
        json j;
        j["color"] = obj.color;
        j["name"] = obj.name;
        j["scale"] = obj.scale;
        j["model"] = model_data;
        json_data.emplace_back(j);
    }

    std::ofstream outfile("output.json");
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open output file." << std::endl;
        return 1;
    }
    outfile << std::setw(4) << json_data; // indent the JSON for readability

    outfile.close();

    return 0;
}


std::vector<Entity> entities_list_tests;



void load() {
    std::ifstream infile("output.json");
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
        entity.model = model;

        entities_list_tests.push_back(entity);
    }
}
Camera3D camera_tests;

int main() {
    load();

    int screenWidth1 = 1900;
    int screenHeight1 = 900;

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth1, screenHeight1, "Lit Engine");
    SetTargetFPS(100000);



    camera_tests.position = { 10.0f, 5.0f, 0.0f };
    camera_tests.target = { 0.0f, 0.0f, 0.0f };
    camera_tests.up = { 0.0f, 1.0f, 0.0f }; // Set the up vector to point along the global +Y axis

    Vector3 front = Vector3Subtract(camera_tests.target, camera_tests.position);
    front = Vector3Normalize(front); // Normalize the front vector to ensure it has a length of 1

    camera_tests.fovy = 60.0f;
    camera_tests.projection = CAMERA_PERSPECTIVE;


    while (!WindowShouldClose())
    {

        BeginDrawing();
        ClearBackground(DARKGRAY);

        BeginMode3D(camera_tests);

            entities_list_tests[0].draw();
        EndMode3D();
        EndDrawing();
    }

}