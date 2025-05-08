/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <NodeEditor/imgui_node_editor.h>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <string>
#include <fstream>
#include <array>
#include <nlohmann/json.hpp>
#include <cstring>
#include <iostream>

namespace fs = std::filesystem;
namespace ed = ax::NodeEditor;
using json = nlohmann::json;

template std::optional<OneMinusXNode*> ChildMaterial::GetNodeData<OneMinusXNode>(const std::string& uuid);
template std::optional<MultiplyNode*>  ChildMaterial::GetNodeData<MultiplyNode>(const std::string& uuid);
template std::optional<MaterialNode*>  ChildMaterial::GetNodeData<MaterialNode>(const std::string& uuid);
template std::optional<TextureNode*>   ChildMaterial::GetNodeData<TextureNode>(const std::string& uuid);
template std::optional<Vector2Node*>   ChildMaterial::GetNodeData<Vector2Node>(const std::string& uuid);
template std::optional<SliderNode*>    ChildMaterial::GetNodeData<SliderNode>(const std::string& uuid);
template std::optional<ColorNode*>     ChildMaterial::GetNodeData<ColorNode>(const std::string& uuid);

std::unordered_map<fs::path, ChildMaterial> childMaterials;

static const std::unordered_map<NodeType, std::string> nodeTypeToString = {
    { NodeType::Color,    "Color"    },
    { NodeType::Texture,  "Texture"  },
    { NodeType::Slider,   "Slider"   },
    { NodeType::Vector2,  "Vector2"  }
};

static const std::unordered_map<std::string, NodeType> stringToNodeType = {
    { "Color",    NodeType::Color     },
    { "Texture",  NodeType::Texture   },
    { "Slider",   NodeType::Slider    },
    { "Vector2",  NodeType::Vector2   }
};

template <typename T>
std::optional<T*> ChildMaterial::GetNodeData(const std::string& uuid) {
    auto it = nodes.find(uuid);
    if (it != nodes.end()) {
        if (auto ptr = std::get_if<T>(&it->second))
            return ptr;
    }
    return std::nullopt;
}

bool IsSafeSavePath(const fs::path& projectRoot, const fs::path& target) noexcept {
    const fs::path canonRoot   = fs::weakly_canonical(projectRoot);
    const fs::path canonTarget = fs::weakly_canonical(target);
    return std::mismatch(canonRoot.begin(), canonRoot.end(), canonTarget.begin()).first == canonRoot.end();
}

void ChildMaterial::SyncWithBlueprint() {
    auto bpIt = materialBlueprints.find(blueprintPath);
    if (bpIt == materialBlueprints.end()) {
        return;
    }

    const auto& blueprintNodes = bpIt->second.nodeSystem.m_Nodes;

    std::unordered_set<std::string> blueprintUUIDs;
    blueprintUUIDs.reserve(blueprintNodes.size());
    for (auto const& node : blueprintNodes) {
        blueprintUUIDs.insert(node.UUID);
    }

    std::erase_if(nodes, [&](auto const& entry) {
        return !blueprintUUIDs.contains(entry.first);
    });

    for (auto const& bpNode : blueprintNodes) {
        switch (bpNode.type) {
            case NodeType::Color:
                nodes.try_emplace(bpNode.UUID, ColorNode{});
                break;
            case NodeType::Texture:
                nodes.try_emplace(bpNode.UUID, TextureNode{});
                break;
            case NodeType::Slider:
                nodes.try_emplace(bpNode.UUID, SliderNode{});
                break;
            case NodeType::Vector2:
                nodes.try_emplace(bpNode.UUID, Vector2Node{});
                break;
            default:
                break;
        }
    }
}

json SerializeNodeData(const Node& blueprintNode, const ChildMaterial& childMaterial) {
    json nodeJson;
    auto typeIt = nodeTypeToString.find(blueprintNode.type);
    nodeJson["type"] = (typeIt != nodeTypeToString.end()) ? typeIt->second : "Unknown";

    auto it = childMaterial.nodes.find(blueprintNode.UUID);
    if (it == childMaterial.nodes.end()) {
        nodeJson["data"] = {};
    } else {
        nodeJson["data"] = std::visit(
            [&](auto& node) -> json {
                using NodeType = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<NodeType, ColorNode>) {
                    return json{node.color.Value.x, node.color.Value.y, node.color.Value.z, node.color.Value.w};
                } else if constexpr (std::is_same_v<NodeType, TextureNode>) {
                    return json{node.texturePath.string()};
                } else if constexpr (std::is_same_v<NodeType, SliderNode>) {
                    return json{node.value};
                } else if constexpr (std::is_same_v<NodeType, Vector2Node>) {
                    return json{node.vec[0], node.vec[1]};
                } else {
                    return json{};
                }
            },
            it->second
        );
    }

    return nodeJson;
}

json SerializeMaterialNodes(const MaterialBlueprint& blueprint, const ChildMaterial& childMaterial) {
    json nodesJson;

    for (const Node& blueprintNode : blueprint.nodeSystem.m_Nodes) {
        if (blueprintNode.type == NodeType::Material ||
            blueprintNode.type == NodeType::OneMinusX ||
            blueprintNode.type == NodeType::Multiply) {
            continue;
        }

        nodesJson[blueprintNode.UUID] = SerializeNodeData(blueprintNode, childMaterial);
    }

    return nodesJson;
}

void SaveJsonToFile(const fs::path& path, const json& j) {
    std::ofstream file(path);
    if (!file.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open file for writing: %s", path.string().c_str());
        return;
    }
    file << j.dump(2);
    TraceLog(LOG_INFO, "material saved successfully to: %s", path.string().c_str());
}

void SaveChildMaterial(const fs::path& path, const ChildMaterial& childMaterial) {
    if (!IsSafeSavePath("project/", path))
        return;

    auto blueprintIt = materialBlueprints.find(childMaterial.blueprintPath);
    if (blueprintIt == materialBlueprints.end()) {
        TraceLog(LOG_ERROR, "Cannot save material: blueprint not found.");
        return;
    }

    MaterialBlueprint& blueprint = blueprintIt->second;

    json j;
    j["name"] = childMaterial.name;
    j["Blueprint"] = childMaterial.blueprintPath.string();
    j["nodeData"] = SerializeMaterialNodes(blueprint, childMaterial);

    SaveJsonToFile(path, j);
}

bool IsValidMaterialFile(const fs::path& path) {
    if (!fs::exists(path)) {
        TraceLog(LOG_ERROR, "Material file does not exist: %s", path.string().c_str());
        return false;
    }
    return true;
}

std::optional<json> LoadJsonFromFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        TraceLog(LOG_ERROR, "Failed to open material file: %s", path.string().c_str());
        return std::nullopt;
    }

    json jsonData;
    try {
        file >> jsonData;
    } catch (const json::parse_error& e) {
        TraceLog(LOG_ERROR, "Failed to parse JSON: %s", e.what());
        return std::nullopt;
    }

    return jsonData;
}

bool EnsureBlueprintLoaded(const fs::path& blueprintPath) {
    if (!materialBlueprints.contains(blueprintPath)) {
        LoadMaterialBlueprint(blueprintPath);
    }

    if (!materialBlueprints.contains(blueprintPath)) {
        TraceLog(LOG_ERROR, "Failed to load blueprint: %s", blueprintPath.string().c_str());
        return false;
    }

    return true;
}

void ParseNodeData(const std::string& uuid, const json& nodeJson, ChildMaterial& childMaterial) {
    if (!nodeJson.contains("type") || !nodeJson.contains("data")) {
        TraceLog(LOG_WARNING, "Invalid node data format for UUID %s", uuid.c_str());
        return;
    }

    std::string type = nodeJson["type"];
    const auto& data = nodeJson["data"];

    try {
        if (type == "Color") {
            ColorNode node;
            if (!data.is_array() || data.size() < 4) {
                node.color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            } else {
                node.color = ImVec4(
                    data[0].is_null() ? 1.0f : data[0].get<float>(),
                    data[1].is_null() ? 1.0f : data[1].get<float>(),
                    data[2].is_null() ? 1.0f : data[2].get<float>(),
                    data[3].is_null() ? 1.0f : data[3].get<float>()
                );
            }
            childMaterial.nodes[uuid] = node;
        } else if (type == "Texture") {
            TextureNode node;
            node.texturePath = (!data[0].is_string() || data[0].is_null()) ? "" : data[0].get<std::string>();
            if (node.texturePath.empty()) {
                TraceLog(LOG_WARNING, "Texture path is empty for UUID %s", uuid.c_str());
                return;
            }
            node.texture = node.texturePath;
            childMaterial.nodes[uuid] = node;
        } else if (type == "Slider") {
            SliderNode node;
            node.value = (data.is_number()) ? data.get<float>() : 0.0f;
            childMaterial.nodes[uuid] = node;
        } else if (type == "Vector2") {
            Vector2Node node;
            node.vec[0] = (!data.is_array() || data.size() < 2 || data[0].is_null()) ? 0.0f : data[0].get<float>();
            node.vec[1] = (!data.is_array() || data.size() < 2 || data[1].is_null()) ? 0.0f : data[1].get<float>();
            childMaterial.nodes[uuid] = node;
        }
    } catch (const json::exception& e) {
        TraceLog(LOG_WARNING, "Failed to parse node data for UUID %s: %s", uuid.c_str(), e.what());
    }
}

void LoadChildMaterial(const fs::path& path) {
    if (!IsValidMaterialFile(path)) return;

    auto jsonDataOpt = LoadJsonFromFile(path);
    if (!jsonDataOpt.has_value()) return;

    json& jsonData = jsonDataOpt.value();

    if (!jsonData.contains("Blueprint") || !jsonData["Blueprint"].is_string()) {
        TraceLog(LOG_ERROR, "Missing or invalid blueprint in material");
        return;
    }

    fs::path blueprintPath = jsonData["Blueprint"].get<std::string>();
    if (!EnsureBlueprintLoaded(blueprintPath)) return;

    const MaterialBlueprint& blueprint = materialBlueprints.at(blueprintPath);

    ChildMaterial childMaterial;
    childMaterial.path = path;
    childMaterial.blueprintPath = blueprintPath;
    childMaterial.name = jsonData.value("name", "Unnamed Child Material");
    childMaterial.materialBlueprintUUID = blueprint.UUID;

    if (jsonData.contains("nodeData") && jsonData["nodeData"].is_object()) {
        for (const auto& [uuid, nodeJson] : jsonData["nodeData"].items()) {
            bool foundInBlueprint = std::any_of(
                blueprint.nodeSystem.m_Nodes.begin(),
                blueprint.nodeSystem.m_Nodes.end(),
                [&](const Node& n) { return n.UUID == uuid; }
            );

            if (!foundInBlueprint) {
                TraceLog(LOG_WARNING, "Node UUID %s from material not found in blueprint", uuid.c_str());
                continue;
            }

            ParseNodeData(uuid, nodeJson, childMaterial);
        }
    }

    childMaterials.emplace(path, childMaterial);
    TraceLog(LOG_INFO, "Successfully loaded material: %s", path.string().c_str());
}