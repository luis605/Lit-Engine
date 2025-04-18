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
    MaterialBlueprint blueprint;
    blueprint.nodeSystem.Init();
    blueprint.materialPath = filePath;

    ed::SetCurrentEditor(blueprint.nodeSystem.m_Context);

    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open material blueprint file: %s", filePath.string().c_str());
        blueprint.name = "Unnamed";
        blueprint.UUID = GenUUID();
        materialBlueprints.emplace(filePath, std::move(blueprint));
        ed::SetCurrentEditor(nullptr);
        return;
    }

    json jsonData;

    try {
        file >> jsonData;
    } catch (const std::exception& e) {
        TraceLog(LOG_ERROR, "Failed to parse blueprint JSON: %s", e.what());
        blueprint.name = "Unnamed";
        blueprint.UUID = GenUUID();
        materialBlueprints.emplace(filePath, std::move(blueprint));
        ed::SetCurrentEditor(nullptr);
        return;
    }

    blueprint.name = jsonData.value("name", "Unnamed");
    blueprint.UUID = jsonData.value("UUID", GenUUID());
    if (blueprint.UUID.empty()) blueprint.UUID = GenUUID();

    const auto& graph = jsonData.value("graph", json::object());
    const auto& nodesJson = graph.value("nodes", json::array());
    const auto& connectionsJson = graph.value("connections", json::array());

    if (graph.empty()) {
        TraceLog(LOG_WARNING, "Missing or invalid 'graph' section in material blueprint.");
    }

    if (nodesJson.is_array()) {
        int nodeIndex = 0;
        for (const auto& nodeJson : nodesJson) {
            const std::string type = nodeJson.value("type", "");
            if (type.empty()) {
                TraceLog(LOG_WARNING, "Node at index %d missing 'type', skipping.", nodeIndex++);
                continue;
            }

            ImVec2 nodePosition(0.0f, 0.0f);
            if (nodeJson.contains("position") && nodeJson["position"].is_array() && nodeJson["position"].size() == 2) {
                nodePosition.x = nodeJson["position"][0].get<float>();
                nodePosition.y = nodeJson["position"][1].get<float>();
            }

            if      (type == "Material")   blueprint.nodeSystem.SpawnMaterialNode();
            else if (type == "Color")      blueprint.nodeSystem.SpawnColorNode();
            else if (type == "Texture")    blueprint.nodeSystem.SpawnTextureNode();
            else if (type == "Slider")     blueprint.nodeSystem.SpawnSliderNode();
            else if (type == "OneMinusX")  blueprint.nodeSystem.SpawnOneMinusXNode();
            else if (type == "Multiply")   blueprint.nodeSystem.SpawnMultiplyNode();
            else if (type == "Vector2")    blueprint.nodeSystem.SpawnVector2Node();
            else {
                TraceLog(LOG_WARNING, "Unknown node type: %s", type.c_str());
                nodeIndex++;
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

    if (connectionsJson.is_array()) {
        for (const auto& connectionJson : connectionsJson) {
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

            blueprint.nodeSystem.m_Links.emplace_back(
                blueprint.nodeSystem.GetNextId(),
                fromNode.Outputs[fromSlot].ID,
                toNode.Inputs[toSlot].ID
            );
        }
    } else {
        TraceLog(LOG_WARNING, "No valid 'connections' array found in material blueprint.");
    }

    materialBlueprints.emplace(filePath, std::move(blueprint));
    ed::SetCurrentEditor(nullptr);
}

void SaveMaterialBlueprints(const fs::path& filePath, const MaterialBlueprint& blueprint) {
    ed::SetCurrentEditor(blueprint.nodeSystem.m_Context);

    json jsonData;
    jsonData["name"] = blueprint.name;
    jsonData["UUID"] = blueprint.UUID;

    json& graph = jsonData["graph"];
    graph["nodes"] = json::array();

    for (const Node& node : blueprint.nodeSystem.m_Nodes) {
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

        ImVec2 pos = ed::GetNodePosition(node.ID);
        nodeData["position"] = {pos.x, pos.y};
        nodeData["UUID"]     = node.UUID;
        graph["nodes"].emplace_back(std::move(nodeData));
    }

    std::unordered_map<uint64_t, std::pair<size_t, size_t>> outputPinMap;
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> inputPinMap;
    for (size_t nodeIdx = 0; nodeIdx < blueprint.nodeSystem.m_Nodes.size(); ++nodeIdx) {
        const auto& node = blueprint.nodeSystem.m_Nodes[nodeIdx];
        for (size_t pinIdx = 0; pinIdx < node.Outputs.size(); ++pinIdx)
            outputPinMap[node.Outputs[pinIdx].ID.Get()] = {nodeIdx, pinIdx};
        for (size_t pinIdx = 0; pinIdx < node.Inputs.size(); ++pinIdx)
            inputPinMap[node.Inputs[pinIdx].ID.Get()] = {nodeIdx, pinIdx};
    }

    graph["connections"] = json::array();
    for (const auto& link : blueprint.nodeSystem.m_Links) {
        json connection;
        auto fromIt = outputPinMap.find(link.StartPinID.Get());
        auto toIt = inputPinMap.find(link.EndPinID.Get());
        if (fromIt != outputPinMap.end() && toIt != inputPinMap.end()) {
            connection["from"] = fromIt->second.first;
            connection["fromSlot"] = fromIt->second.second;
            connection["to"] = toIt->second.first;
            connection["toSlot"] = toIt->second.second;
            graph["connections"].push_back(std::move(connection));
        }
    }

    std::ofstream file(filePath);
    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open file for writing: %s", filePath.string().c_str());
        ed::SetCurrentEditor(nullptr);
        return;
    }
    file << jsonData.dump(4);

    for (auto& [_, childMaterial] : childMaterials) {
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