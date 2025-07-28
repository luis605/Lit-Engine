/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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
    blueprint.nodeSystem.Initialize();
    blueprint.materialPath = filePath;

    ed::SetCurrentEditor(blueprint.nodeSystem.m_context);

    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open material blueprint file: %s", filePath.string().c_str());
        blueprint.name = "Unnamed";
        blueprint.m_uuid = GenUUID();
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
        blueprint.m_uuid = GenUUID();
        materialBlueprints.emplace(filePath, std::move(blueprint));
        ed::SetCurrentEditor(nullptr);
        return;
    }

    blueprint.name = jsonData.value("name", "Unnamed");
    blueprint.m_uuid = jsonData.value("UUID", GenUUID());
    if (blueprint.m_uuid.empty()) blueprint.m_uuid = GenUUID();

    const auto& graph = jsonData.value("graph", json::object());
    const auto& nodesJson = graph.value("nodes", json::array());
    const auto& connectionsJson = graph.value("connections", json::array());

    if (graph.empty()) {
        TraceLog(LOG_WARNING, "Missing or invalid 'graph' section in material blueprint.");
    }

    std::vector<std::optional<ed::NodeId>> nodeIndexToId;

    if (nodesJson.is_array()) {
        for (const auto& nodeJson : nodesJson) {
            const std::string type = nodeJson.value("type", "");
            if (type.empty()) {
                TraceLog(LOG_WARNING, "Node at index %d missing 'type', skipping.", nodeIndexToId.size());
                nodeIndexToId.push_back(std::nullopt);
                continue;
            }

            ImVec2 nodePosition(0.0f, 0.0f);
            if (nodeJson.contains("position") && nodeJson["position"].is_array() && nodeJson["position"].size() == 2) {
                nodePosition.x = nodeJson["position"][0].get<float>();
                nodePosition.y = nodeJson["position"][1].get<float>();
            }

            Node* node = nullptr;
            if      (type == "Material")   node = blueprint.nodeSystem.SpawnMaterialNode();
            else if (type == "Color")      node = blueprint.nodeSystem.SpawnColorNode();
            else if (type == "Texture")    node = blueprint.nodeSystem.SpawnTextureNode();
            else if (type == "Slider")     node = blueprint.nodeSystem.SpawnSliderNode();
            else if (type == "OneMinusX")  node = blueprint.nodeSystem.SpawnOneMinusXNode();
            else if (type == "Multiply")   node = blueprint.nodeSystem.SpawnMultiplyNode();
            else if (type == "Vector2")    node = blueprint.nodeSystem.SpawnVector2Node();
            else {
                TraceLog(LOG_WARNING, "Unknown node type: %s", type.c_str());
                nodeIndexToId.push_back(std::nullopt);
                continue;
            }

            if (!node) {
                TraceLog(LOG_ERROR, "Failed to create node of type: %s", type.c_str());
                nodeIndexToId.push_back(std::nullopt);
                continue;
            }

            ed::SetNodePosition(node->m_id, nodePosition);
            nodeIndexToId.push_back(node->m_id);

            if (nodeJson.contains("UUID")) {
                node->m_uuid = nodeJson["UUID"].get<std::string>();
            }
            node->m_name = nodeJson.value("name", "Node");
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

            if (fromNodeIdx >= nodeIndexToId.size() || !nodeIndexToId[fromNodeIdx].has_value() ||
                toNodeIdx >= nodeIndexToId.size() || !nodeIndexToId[toNodeIdx].has_value()) {
                TraceLog(LOG_WARNING, "Connection references invalid node indices, skipping.");
                continue;
            }

            ed::NodeId fromNodeId = nodeIndexToId[fromNodeIdx].value();
            ed::NodeId toNodeId = nodeIndexToId[toNodeIdx].value();

            Node* fromNode = blueprint.nodeSystem.FindNode(fromNodeId);
            Node* toNode = blueprint.nodeSystem.FindNode(toNodeId);

            if (!fromNode || !toNode) {
                 TraceLog(LOG_WARNING, "Connection references non-existent nodes, skipping.");
                 continue;
            }

            if (fromSlot >= fromNode->m_outputs.size() || toSlot >= toNode->m_inputs.size()) {
                TraceLog(LOG_WARNING, "Connection references invalid pin indices, skipping.");
                continue;
            }

            Pin* startPin = blueprint.nodeSystem.FindPin(fromNode->m_outputs[fromSlot]);
            Pin* endPin = blueprint.nodeSystem.FindPin(toNode->m_inputs[toSlot]);

            if (!startPin || !endPin) {
                TraceLog(LOG_WARNING, "Could not find pins for connection, skipping.");
                continue;
            }

            ed::LinkId newLinkId = blueprint.nodeSystem.GetNextLinkId();
            blueprint.nodeSystem.AddLink(newLinkId, startPin->m_id, endPin->m_id);
        }
    } else {
        TraceLog(LOG_WARNING, "No valid 'connections' array found in material blueprint.");
    }

    materialBlueprints.emplace(filePath, std::move(blueprint));
    ed::SetCurrentEditor(nullptr);
}

void SaveMaterialBlueprints(const fs::path& filePath, const MaterialBlueprint& blueprint) {
    ed::SetCurrentEditor(blueprint.nodeSystem.m_context);

    json jsonData;
    jsonData["name"] = blueprint.name;
    jsonData["UUID"] = blueprint.m_uuid;

    json& graph = jsonData["graph"];
    graph["nodes"] = json::array();

    std::unordered_map<uint64_t, std::pair<size_t, size_t>> outputPinMap;
    std::unordered_map<uint64_t, std::pair<size_t, size_t>> inputPinMap;

    size_t nodeJsonIndex = 0;
    for (const auto& [nodeId, blueprintNode] : blueprint.nodeSystem.m_nodes) {
        json nodeData;

        switch (blueprintNode.m_type) {
            case NodeType::Material:    nodeData["type"] = "Material";  break;
            case NodeType::Color:       nodeData["type"] = "Color";     break;
            case NodeType::Texture:     nodeData["type"] = "Texture";   break;
            case NodeType::Slider:      nodeData["type"] = "Slider";    break;
            case NodeType::OneMinusX:   nodeData["type"] = "OneMinusX"; break;
            case NodeType::Multiply:    nodeData["type"] = "Multiply";  break;
            case NodeType::Vector2:     nodeData["type"] = "Vector2";   break;
            default:                    nodeData["type"] = "Unknown";   break;
        }

        ImVec2 pos = ed::GetNodePosition(blueprintNode.m_id);
        nodeData["position"] = {pos.x, pos.y};
        nodeData["UUID"]     = blueprintNode.m_uuid;
        nodeData["name"]     = blueprintNode.m_name;
        graph["nodes"].emplace_back(std::move(nodeData));

        for (size_t pinIdx = 0; pinIdx < blueprintNode.m_outputs.size(); ++pinIdx) {
            outputPinMap[blueprintNode.m_outputs[pinIdx].Get()] = {nodeJsonIndex, pinIdx};
        }
        for (size_t pinIdx = 0; pinIdx < blueprintNode.m_inputs.size(); ++pinIdx) {
            inputPinMap[blueprintNode.m_inputs[pinIdx].Get()] = {nodeJsonIndex, pinIdx};
        }

        nodeJsonIndex++;
    }

    graph["connections"] = json::array();
    for (const auto& [linkId, link] : blueprint.nodeSystem.m_links) {
        json connection;
        auto fromIt = outputPinMap.find(link.m_startPinId.Get());
        auto toIt = inputPinMap.find(link.m_endPinId.Get());
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
    newBlueprint.m_uuid = GenUUID();
    materialBlueprints.emplace(path, newBlueprint);
}