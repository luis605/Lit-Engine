#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialGraph.hpp>
#include <Engine/Editor/MaterialNodeEditor/ChildMaterial.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialShaderGenerator.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <fstream>
#include <raylib.h>
#include <string>
#include <optional>
#include <functional>

namespace {
    enum MaterialProperty {
        Albedo = 0,
        Normal = 1,
        Roughness = 2,
        AmbientOcclusion = 3,
        Height = 4,
        Metallic = 5,
        Emission = 6,
        Tiling = 7
    };
}

std::string GenerateMaterialShader(ChildMaterial& material) {
    std::optional<std::reference_wrapper<MaterialBlueprint>> blueprintRef;

    auto it = materialBlueprints.find(material.blueprintPath);
    if (it != materialBlueprints.end()) {
        blueprintRef = std::ref(it->second);
    } else {
        TraceLog(LOG_ERROR, "Child Material does not inherit any blueprint.");
        return {0};
    }

    MaterialBlueprint& blueprint = blueprintRef->get();

    MaterialTree tree = MaterialTree::BuildTree(blueprint.nodeSystem.m_Nodes, blueprint.nodeSystem.m_Links);

    if (!tree.root) {
        TraceLog(LOG_ERROR, "Material tree has no valid root node.");
        return {0};
    }

    std::function<std::string(TreeNode*)> GenerateNodeShaderCode =
        [&](TreeNode* treeNode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_WARNING, "Null tree node encountered.");
            return "vec4(0.0)";
        }

        Node* node = blueprint.nodeSystem.FindNode(treeNode->GetId());
        if (!node) {
            TraceLog(LOG_WARNING, "Null node encountered.");
            return "vec4(0.0)";
        }

        const std::string& hash = node->UUID;
        switch (node->type) {
            case NodeType::Texture:
                return "textureRGBA";

            case NodeType::Color: {
                std::optional<ColorNode*> nodeData = material.GetNodeData<ColorNode>(hash);
                if (!nodeData) return "vec4(0.0)";
                return std::string("vec4(" +
                       std::to_string(nodeData.value()->color.Value.x) + ", " +
                       std::to_string(nodeData.value()->color.Value.y) + ", " +
                       std::to_string(nodeData.value()->color.Value.z) + ", " +
                       std::to_string(nodeData.value()->color.Value.w) + ")");
            }

            case NodeType::Slider: {
                std::optional<SliderNode*> nodeData = material.GetNodeData<SliderNode>(hash);
                return nodeData ? std::to_string(nodeData.value()->value) : "0.0";
            }

            case NodeType::OneMinusX: {
                auto inputNode = treeNode->GetInputs().begin()->second.front().nodeID;
                return "1.0 - " + GenerateNodeShaderCode(tree.nodes[inputNode].get());
            }

            case NodeType::Multiply: {
                auto inputIter = treeNode->GetInputs().begin();
                auto input1NodeId = inputIter->second.front().nodeID;
                auto input2NodeId = (++inputIter)->second.front().nodeID;
                return "(" + GenerateNodeShaderCode(tree.nodes[input1NodeId].get()) +
                       " * " + GenerateNodeShaderCode(tree.nodes[input2NodeId].get()) + ")";
            }

            default:
                TraceLog(LOG_WARNING, "Unsupported node type.");
                return "vec4(0.0)";
        }
    };

    auto ExtractConnections = [](TreeNode* node) -> std::vector<std::pair<int, Connection>> {
        std::vector<std::pair<int, Connection>> connections;
        for (const auto& [pinId, connList] : node->GetInputs()) {
            for (const auto& conn : connList) {
                connections.emplace_back(pinId, conn);
            }
        }
        return connections;
    };

    std::function<std::string(TreeNode*, const ed::PinId&, const std::string&)>
    ProcessTreeNode = [&](TreeNode* treeNode, const ed::PinId& nodePinId,
                         const std::string& defaultCode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_ERROR, "Invalid tree node.");
            return defaultCode;
        }

        std::string accumulatedCode;
        auto allConnections = ExtractConnections(treeNode);

        for (const auto& [pinId, conn] : allConnections) {
            if ((nodePinId.Get() != -1) && (pinId != nodePinId.Get())) continue;

            auto it = tree.nodes.find(conn.nodeID);
            if (it == tree.nodes.end()) {
                TraceLog(LOG_WARNING, "Node with ID %d not found.", conn.nodeID);
                continue;
            }

            accumulatedCode += GenerateNodeShaderCode(it->second.get());
            ProcessTreeNode(it->second.get(), ed::PinId(-1), "");
        }

        return accumulatedCode.empty() ? defaultCode : accumulatedCode;
    };

    Node* rootNode = blueprint.nodeSystem.FindNode(tree.root->GetId());
    if (!rootNode) return {0};

    std::stringstream shaderStream;

    std::function<Node*(TreeNode*, std::function<bool(const Node*)>)> findNode =
        [&](TreeNode* treeNode, std::function<bool(const Node*)> predicate) -> Node* {
            if (!treeNode) return nullptr;

            Node* currentNode = blueprint.nodeSystem.FindNode(treeNode->GetId());
            if (currentNode && predicate(currentNode)) return currentNode;

            for (const auto& [pinId, connections] : treeNode->GetInputs()) {
                for (const auto& conn : connections) {
                    auto it = tree.nodes.find(conn.nodeID);
                    if (it != tree.nodes.end()) {
                        if (Node* foundNode = findNode(it->second.get(), predicate))
                            return foundNode;
                    }
                }
            }
            return nullptr;
        };

    for (size_t pinIndex = 0; pinIndex < rootNode->Inputs.size(); ++pinIndex) {
        const Pin& pin = rootNode->Inputs[pinIndex];
        auto inputConnsIt = tree.root->GetInputs().find(pin.ID.Get());
        if (inputConnsIt == tree.root->GetInputs().end() || inputConnsIt->second.empty())
            continue;

        const Connection& conn = inputConnsIt->second.front();
        auto connectedNodeIt = tree.nodes.find(conn.nodeID);
        if (connectedNodeIt == tree.nodes.end())
            continue;

        TreeNode* connectedTreeNode = connectedNodeIt->second.get();
        Node* node = nullptr;

        if (pinIndex <= MaterialProperty::Emission) {
            node = findNode(connectedTreeNode, [](const Node* n) {
                return n->type == NodeType::Texture;
            });
            if (!node) continue;

            if (std::optional<TextureNode*> textureData = material.GetNodeData<TextureNode>(node->UUID)) {
                switch (pinIndex) {
                    case MaterialProperty::Albedo:
                        selectedEntity->surfaceMaterial.albedoTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.albedoTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define ALBEDO\n";
                        break;
                    case MaterialProperty::Normal:
                        selectedEntity->surfaceMaterial.normalTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.normalTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define NORMAL\n";
                        break;
                    case MaterialProperty::Roughness:
                        selectedEntity->surfaceMaterial.roughnessTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.roughnessTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define ROUGHNESS\n";
                        break;
                    case MaterialProperty::AmbientOcclusion:
                        selectedEntity->surfaceMaterial.aoTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.aoTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define AMBIENT_OCCLUSION\n";
                        break;
                    case MaterialProperty::Height:
                        selectedEntity->surfaceMaterial.heightTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.heightTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define HEIGHT\n";
                        break;
                    case MaterialProperty::Metallic:
                        selectedEntity->surfaceMaterial.metallicTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.metallicTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define METALNESS\n";
                        break;
                    case MaterialProperty::Emission:
                        selectedEntity->surfaceMaterial.emissiveTexture = textureData.value()->texture;
                        selectedEntity->surfaceMaterial.emissiveTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define EMISSIVE\n";
                        break;
                }
            }
        }
        else if (pinIndex == MaterialProperty::Tiling) {
            node = findNode(connectedTreeNode, [](const Node* n) {
                return n->type == NodeType::Vector2;
            });
            if (!node) continue;

            if (std::optional<Vector2Node*> vec2NodeData = material.GetNodeData<Vector2Node>(node->UUID)) {
                selectedEntity->surfaceMaterial.tiling[0] = vec2NodeData.value()->vec[0];
                selectedEntity->surfaceMaterial.tiling[1] = vec2NodeData.value()->vec[1];
            }
        }
    }

    const std::pair<const char*, size_t> materialFuncs[] = {
        {"calcDiffuseMap", 0},
        {"calcNormalMap", 1},
        {"calcRoughnessMap", 2},
        {"calcAmbientOcclusionMap", 3},
        {"calcHeightMap", 4},
        {"calcMetallicMap", 5},
        {"calcEmissionMap", 6}
    };

    for (const auto& [funcName, inputIdx] : materialFuncs) {
        shaderStream << "vec4 " << funcName << "(vec4 textureRGBA) {\n  return "
                    << ProcessTreeNode(tree.root, rootNode->Inputs.at(inputIdx).ID, "textureRGBA")
                    << ";\n}\n\n";
    }



    std::ifstream stream("Engine/Lighting/shaders/lighting_fragment.glsl");
    if (!stream.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open default fragment shader file.");
        return { 0 };
    }

    std::string defaultShaderCode = std::string((std::istreambuf_iterator<char>(stream)),
                                    std::istreambuf_iterator<char>());

    static const std::string placeholder = "// [ INSERT GENERATED CODE BELOW ]";
    size_t pos = defaultShaderCode.find(placeholder);
    if (pos != std::string::npos) {
        defaultShaderCode.replace(pos, placeholder.length(), shaderStream.str());
    }

    return defaultShaderCode;
}