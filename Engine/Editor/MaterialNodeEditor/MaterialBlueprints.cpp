#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Lighting/SurfaceMaterial.hpp>
#include <Engine/Core/UUID.hpp>
#include <vector>
#include <filesystem>
#include <string>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

std::unordered_map<fs::path, MaterialBlueprint> materialBlueprints;

void LoadMaterialBlueprint(const fs::path& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open material blueprint file: %s", filePath.string().c_str());
        return;
    }

    MaterialBlueprint blueprint;
    blueprint.nodeSystem.Init();
    blueprint.materialPath = filePath;

    ed::SetCurrentEditor(blueprint.nodeSystem.m_Context);

    json jsonData;
    try {
        file >> jsonData;
    } catch (const std::exception& e) {
        TraceLog(LOG_ERROR, "Failed to parse JSON: %s", e.what());
        blueprint.UUID = GenUUID();
        materialBlueprints.emplace(filePath, blueprint);
        return;
    }

    file.close();

    blueprint.name = jsonData.value("name", "Unnamed");

    if (jsonData.contains("UUID") && jsonData["UUID"].is_string()) {
        blueprint.UUID = jsonData["UUID"].get<std::string>();
        if (blueprint.UUID.empty()) blueprint.UUID = GenUUID();
    } else {
        TraceLog(LOG_WARNING, "Missing or invalid 'UUID' in material blueprint.");
        blueprint.UUID = GenUUID();
    }

    if (!jsonData.contains("graph") || !jsonData["graph"].is_object()) {
        TraceLog(LOG_WARNING, "Missing or invalid 'graph' section in material blueprint.");
        materialBlueprints.emplace(filePath, blueprint);
        return;
    }

    const json& graph = jsonData["graph"];

    const auto& nodesJson = graph.find("nodes");
    if (nodesJson != graph.end() && nodesJson->is_array()) {
        int nodeIndex = 0;
        for (const auto& nodeJson : *nodesJson) {
            const std::string type = nodeJson.value("type", "");

            if (type.empty()) {
                TraceLog(LOG_WARNING, "Node at index %d missing 'type', skipping.", nodeIndex);
                continue;
            }

            ImVec2 nodePosition(0.0f, 0.0f);

            if (nodeJson.contains("position") && nodeJson["position"].is_array() && nodeJson["position"].size() == 2) {
                nodePosition.x = nodeJson["position"][0].get<float>();
                nodePosition.y = nodeJson["position"][1].get<float>();
            }

            if (type == "Material") {
                blueprint.nodeSystem.SpawnMaterialNode();
            } else if (type == "Color") {
                blueprint.nodeSystem.SpawnColorNode();
            } else if (type == "Texture")    {
                blueprint.nodeSystem.SpawnTextureNode();
            } else if (type == "Slider")     {
                blueprint.nodeSystem.SpawnSliderNode();
            } else if (type == "OneMinusX")  {
                blueprint.nodeSystem.SpawnOneMinusXNode();
            } else if (type == "Multiply")   {
                blueprint.nodeSystem.SpawnMultiplyNode();
            } else if (type == "Vector2")    {
                blueprint.nodeSystem.SpawnVector2Node();
            } else {
                TraceLog(LOG_WARNING, "Unknown node type: %s", type.c_str());
                continue;
            }
            ed::SetNodePosition(blueprint.nodeSystem.m_Nodes.back().ID, nodePosition);

            if (nodeJson.contains("UUID")) {
                blueprint.nodeSystem.m_Nodes.back().UUID = nodeJson["UUID"].get<std::string>();
            }

            nodeIndex++;
        }
    } else {
        TraceLog(LOG_WARNING, "No valid 'nodes' array found in material blueprint.");
    }

    const auto& connectionsJson = graph.find("connections");
    if (connectionsJson != graph.end() && connectionsJson->is_array()) {
        for (const auto& connectionJson : *connectionsJson) {
            if (!connectionJson.contains("from") || !connectionJson.contains("to") ||
                !connectionJson.contains("fromSlot") || !connectionJson.contains("toSlot")) {
                TraceLog(LOG_WARNING, "Connection missing required fields, skipping.");
                continue;
            }

            size_t fromNodeIdx = connectionJson["from"].get<size_t>();
            size_t toNodeIdx = connectionJson["to"].get<size_t>();
            size_t fromSlot = connectionJson["fromSlot"].get<size_t>();
            size_t toSlot = connectionJson["toSlot"].get<size_t>();

            if (fromNodeIdx >= blueprint.nodeSystem.m_Nodes.size() ||
                toNodeIdx >= blueprint.nodeSystem.m_Nodes.size()) {
                TraceLog(LOG_WARNING, "Connection references invalid node indices, skipping.");
                continue;
            }

            const auto& fromNode = blueprint.nodeSystem.m_Nodes[fromNodeIdx];
            const auto& toNode = blueprint.nodeSystem.m_Nodes[toNodeIdx];

            if (fromSlot >= fromNode.Outputs.size() || toSlot >= toNode.Inputs.size()) {
                TraceLog(LOG_WARNING, "Connection references invalid pin indices, skipping.");
                continue;
            }

            ed::PinId startPinId = fromNode.Outputs[fromSlot].ID;
            ed::PinId endPinId = toNode.Inputs[toSlot].ID;
            blueprint.nodeSystem.m_Links.emplace_back(
                blueprint.nodeSystem.GetNextId(),
                startPinId,
                endPinId
            );
        }
    } else {
        TraceLog(LOG_WARNING, "No valid 'connections' array found in material blueprint.");
    }

    materialBlueprints.emplace(filePath, blueprint);

    ed::SetCurrentEditor(nullptr);
}


void SaveMaterialBlueprints(const fs::path& filePath, const MaterialBlueprint& blueprint) {
    ed::SetCurrentEditor(blueprint.nodeSystem.m_Context);

    json jsonData;

    jsonData["name"] = blueprint.name;
    jsonData["UUID"] = blueprint.UUID;

    json& graph = jsonData["graph"];

    graph["nodes"] = json::array();
    for (size_t i = 0; i < blueprint.nodeSystem.m_Nodes.size(); i++) {
        const auto& node = blueprint.nodeSystem.m_Nodes[i];

        json nodeData;

        switch (node.type) {
            case NodeType::Material:    nodeData["type"] = "Material";  break;
            case NodeType::Color:       nodeData["type"] = "Color";     break;
            case NodeType::Texture:     nodeData["type"] = "Texture";   break;
            case NodeType::Slider:      nodeData["type"] = "Slider";    break;
            case NodeType::OneMinusX:   nodeData["type"] = "OneMinusX"; break;
            case NodeType::Multiply:    nodeData["type"] = "Multiply";  break;
            case NodeType::Vector2:     nodeData["type"] = "Vector2";   break;
            default:                    nodeData["type"] = "Unknown";   break;
        }

        ImVec2 position = ed::GetNodePosition(node.ID);
        nodeData["position"] = {position.x, position.y};
        nodeData["UUID"]     = node.UUID;

        graph["nodes"].push_back(nodeData);
    }

    graph["connections"] = json::array();
    for (const auto& link : blueprint.nodeSystem.m_Links) {
        json connection;

        for (size_t nodeIdx = 0; nodeIdx < blueprint.nodeSystem.m_Nodes.size(); nodeIdx++) {
            const auto& node = blueprint.nodeSystem.m_Nodes[nodeIdx];

            for (size_t pinIdx = 0; pinIdx < node.Outputs.size(); pinIdx++) {
                if (node.Outputs[pinIdx].ID == link.StartPinID) {
                    connection["from"] = nodeIdx;
                    connection["fromSlot"] = pinIdx;
                }
            }

            for (size_t pinIdx = 0; pinIdx < node.Inputs.size(); pinIdx++) {
                if (node.Inputs[pinIdx].ID == link.EndPinID) {
                    connection["to"] = nodeIdx;
                    connection["toSlot"] = pinIdx;
                }
            }
        }

        if (connection.contains("from") && connection.contains("to")) {
            graph["connections"].push_back(connection);
        }
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open file for writing: %s", filePath.string().c_str());
        return;
    }

    file << jsonData.dump(4);
    file.close();

    for (auto& [path, childMaterial] : childMaterials) {
        if (childMaterial.blueprintPath == filePath) {
            childMaterial.SyncWithBlueprint();
            SaveChildMaterial(childMaterial.path, childMaterial);
        }
    }

    ed::SetCurrentEditor(nullptr);
}

void CreateMaterialBlueprint(const std::string& name, const fs::path& path) {
    MaterialBlueprint newBlueprint;
    newBlueprint.name = name;
    newBlueprint.materialPath = path;
    newBlueprint.UUID = GenUUID();
    materialBlueprints.emplace(path, newBlueprint);
}