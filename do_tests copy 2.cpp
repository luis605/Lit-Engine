


#include <iostream>
#include <fstream>
#include <vector>
#include "json.hpp" // include the nlohmann/json library
#include "include_all.h"

using json = nlohmann::json; // define a shortcut for the nlohmann::json type



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




int main() {

    InitWindow(1000, 1000, "Lit Engine");

    Model model = LoadModel("assets/models/gizmo/cube.obj");
    json result  = ModelToString(model);

    std::cout << result << std::endl;

    return 0;
}
