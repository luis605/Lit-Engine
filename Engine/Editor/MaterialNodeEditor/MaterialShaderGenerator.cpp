/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

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
#include <sstream>

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

std::string GenerateMaterialShader(Entity& entity, ChildMaterial& material) {
    std::optional<std::reference_wrapper<MaterialBlueprint>> blueprintRef;

    auto it = materialBlueprints.find(material.blueprintPath);
    if (it != materialBlueprints.end()) {
        blueprintRef = std::ref(it->second);
    } else {
        TraceLog(LOG_ERROR, "Child Material does not inherit any blueprint.");
        return {};
    }

    MaterialBlueprint& blueprint = blueprintRef->get();
    MaterialTree tree = MaterialTree::BuildTree(blueprint.nodeSystem.m_nodes, blueprint.nodeSystem.m_links);

    if (!tree.root) {
        TraceLog(LOG_ERROR, "Material tree has no valid root node.");
        return {};
    }

    std::function<std::string(TreeNode*)> GenerateNodeShaderCode =
        [&](TreeNode* treeNode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_WARNING, "Null tree node encountered.");
            return "vec4(0.0)";
        }

        Node* node = blueprint.nodeSystem.FindNode(treeNode->GetId());
        if (!node) {
            TraceLog(LOG_WARNING, "Null blueprint node encountered.");
            return "vec4(0.0)";
        }

        const std::string& hash = node->m_uuid;
        switch (node->m_type) {
            case NodeType::Texture:
                return "textureRGBA";

            case NodeType::Color: {
                auto nodeData = material.GetNodeData<ColorNode>(hash);
                if (!nodeData) return "vec4(0.0)";
                return "vec4(" +
                       std::to_string(nodeData.value()->color.Value.x) + ", " +
                       std::to_string(nodeData.value()->color.Value.y) + ", " +
                       std::to_string(nodeData.value()->color.Value.z) + ", " +
                       std::to_string(nodeData.value()->color.Value.w) + ")";
            }

            case NodeType::Slider: {
                auto nodeData = material.GetNodeData<SliderNode>(hash);
                return nodeData ? std::to_string(nodeData.value()->value) : "0.0";
            }

            case NodeType::OneMinusX: {
                if (node->m_inputs.empty()) return "1.0";
                const auto& inputs = treeNode->GetInputs();
                auto pinIt = inputs.find(node->m_inputs[0].Get());
                if (pinIt == inputs.end() || pinIt->second.empty()) return "1.0";

                auto inputNodeId = pinIt->second.front().nodeID;
                auto inputTreeNodeIt = tree.nodes.find(inputNodeId);
                if (inputTreeNodeIt == tree.nodes.end()) return "1.0";

                return "1.0 - " + GenerateNodeShaderCode(inputTreeNodeIt->second.get());
            }

            case NodeType::Multiply: {
                if (node->m_inputs.size() < 2) return "0.0";
                const auto& inputs = treeNode->GetInputs();

                auto pinIt1 = inputs.find(node->m_inputs[0].Get());
                auto pinIt2 = inputs.find(node->m_inputs[1].Get());

                if (pinIt1 == inputs.end() || pinIt1->second.empty() ||
                    pinIt2 == inputs.end() || pinIt2->second.empty()) {
                    return "0.0";
                }

                auto input1NodeId = pinIt1->second.front().nodeID;
                auto input2NodeId = pinIt2->second.front().nodeID;

                auto inputTreeNodeIt1 = tree.nodes.find(input1NodeId);
                auto inputTreeNodeIt2 = tree.nodes.find(input2NodeId);

                if (inputTreeNodeIt1 == tree.nodes.end() || inputTreeNodeIt2 == tree.nodes.end()) return "0.0";

                return "(" + GenerateNodeShaderCode(inputTreeNodeIt1->second.get()) +
                       " * " + GenerateNodeShaderCode(inputTreeNodeIt2->second.get()) + ")";
            }

            default:
                TraceLog(LOG_WARNING, "Unsupported node type.");
                return "vec4(0.0)";
        }
    };

    std::function<std::string(TreeNode*, const ed::PinId&, const std::string&)>
    ProcessTreeNode = [&](TreeNode* treeNode, const ed::PinId& nodePinId,
                         const std::string& defaultCode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_ERROR, "Invalid tree node.");
            return defaultCode;
        }

        const auto& inputs = treeNode->GetInputs();
        auto pinIt = inputs.find(nodePinId.Get());

        if (pinIt == inputs.end() || pinIt->second.empty()) {
            return defaultCode;
        }

        const Connection& conn = pinIt->second.front();
        auto connectedNodeIt = tree.nodes.find(conn.nodeID);
        if (connectedNodeIt == tree.nodes.end()) {
            TraceLog(LOG_WARNING, "Node with ID %d not found.", conn.nodeID);
            return defaultCode;
        }

        return GenerateNodeShaderCode(connectedNodeIt->second.get());
    };

    Node* rootNode = blueprint.nodeSystem.FindNode(tree.root->GetId());
    if (!rootNode) return {};

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

    for (size_t pinIndex = 0; pinIndex < rootNode->m_inputs.size(); ++pinIndex) {
        const ed::PinId& pinId = rootNode->m_inputs[pinIndex];
        auto inputConnsIt = tree.root->GetInputs().find(pinId.Get());
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
                return n->m_type == NodeType::Texture;
            });
            if (!node) continue;

            if (auto textureData = material.GetNodeData<TextureNode>(node->m_uuid)) {
                switch (pinIndex) {
                    case MaterialProperty::Albedo:
                        entity.surfaceMaterial.albedoTexture = textureData.value()->texture;
                        entity.surfaceMaterial.albedoTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define ALBEDO\n";
                        break;
                    case MaterialProperty::Normal:
                        entity.surfaceMaterial.normalTexture = textureData.value()->texture;
                        entity.surfaceMaterial.normalTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define NORMAL\n";
                        break;
                    case MaterialProperty::Roughness:
                        entity.surfaceMaterial.roughnessTexture = textureData.value()->texture;
                        entity.surfaceMaterial.roughnessTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define ROUGHNESS\n";
                        break;
                    case MaterialProperty::AmbientOcclusion:
                        entity.surfaceMaterial.aoTexture = textureData.value()->texture;
                        entity.surfaceMaterial.aoTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define AMBIENT_OCCLUSION\n";
                        break;
                    case MaterialProperty::Height:
                        entity.surfaceMaterial.heightTexture = textureData.value()->texture;
                        entity.surfaceMaterial.heightTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define HEIGHT\n";
                        break;
                    case MaterialProperty::Metallic:
                        entity.surfaceMaterial.metallicTexture = textureData.value()->texture;
                        entity.surfaceMaterial.metallicTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define METALNESS\n";
                        break;
                    case MaterialProperty::Emission:
                        entity.surfaceMaterial.emissiveTexture = textureData.value()->texture;
                        entity.surfaceMaterial.emissiveTexturePath = textureData.value()->texturePath;
                        shaderStream << "#define EMISSIVE\n";
                        break;
                }
            }
        }
        else if (pinIndex == MaterialProperty::Tiling) {
            node = findNode(connectedTreeNode, [](const Node* node) {
                return node->m_type == NodeType::Vector2;
            });
            if (!node) continue;

            if (auto vec2NodeData = material.GetNodeData<Vector2Node>(node->m_uuid)) {
                shaderStream << "#define TILING\n";
                shaderStream << "vec2 tiling = vec2(" << vec2NodeData.value()->vec[0] << ", "
                             << vec2NodeData.value()->vec[1] << ");\n";
            }
        }
    }

    const std::pair<const char*, size_t> materialFuncs[] = {
        {"calcDiffuseMap", MaterialProperty::Albedo},
        {"calcNormalMap", MaterialProperty::Normal},
        {"calcRoughnessMap", MaterialProperty::Roughness},
        {"calcAmbientOcclusionMap", MaterialProperty::AmbientOcclusion},
        {"calcHeightMap", MaterialProperty::Height},
        {"calcMetallicMap", MaterialProperty::Metallic},
        {"calcEmissionMap", MaterialProperty::Emission}
    };

    for (const auto& [funcName, inputIdx] : materialFuncs) {
        shaderStream << "vec4 " << funcName << "(vec4 textureRGBA) {\n  return "
                    << ProcessTreeNode(tree.root, rootNode->m_inputs.at(inputIdx), "textureRGBA")
                    << ";\n}\n\n";
    }

    std::ifstream stream("Engine/Lighting/shaders/lighting_fragment.glsl");
    if (!stream.is_open()) {
        TraceLog(LOG_ERROR, "Failed to open default fragment shader file.");
        return {};
    }

    std::string defaultShaderCode((std::istreambuf_iterator<char>(stream)),
                                   std::istreambuf_iterator<char>());

    static const std::string placeholder = "// [ INSERT GENERATED CODE BELOW ]";
    size_t pos = defaultShaderCode.find(placeholder);
    if (pos != std::string::npos) {
        defaultShaderCode.replace(pos, placeholder.length(), shaderStream.str());
    }

    return defaultShaderCode;
}