#include <Engine/Core/Engine.hpp>
#include <Engine/Editor/MaterialsNodeEditor/MaterialGraph.hpp>
#include <Engine/Editor/MaterialsNodeEditor/MaterialShaderGenerator.hpp>
#include <fstream>
#include <iostream>
#include <raylib.h>
#include <string>

Shader GenerateMaterialShader() {
    MaterialTree tree = MaterialTree::BuildTree(materialNodeSystem.m_Nodes,
                                                materialNodeSystem.m_Links);

    if (!tree.root) {
        TraceLog(LOG_WARNING, "[ Generate Material Shader ] Material tree has "
                              "no valid root node.");
        return {0};
    }

    std::function<std::string(TreeNode*)> GenerateNodeShaderCode =
        [&](TreeNode* treeNode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_WARNING,
                     "[GenerateNodeShaderCode] Null tree node encountered.");
            return {0};
        }

        Node* node = materialNodeSystem.FindNode(treeNode->GetId());
        if (!node) {
            TraceLog(LOG_WARNING,
                     "[GenerateNodeShaderCode] Null node encountered.");
            return {0};
        }

        switch (node->type) {
        case NodeType::Texture: {
            return "textureRGBA";
        }
        case NodeType::Color: {
            ColorNode* nodeData =
                GetNodeData<ColorNode>(*node).value_or(nullptr);
            if (nodeData) {
                return std::string(
                    "vec4(" + std::to_string(nodeData->color.Value.x) + ", " +
                    std::to_string(nodeData->color.Value.y) + ", " +
                    std::to_string(nodeData->color.Value.z) + ", " +
                    std::to_string(nodeData->color.Value.w) + ")");
            }
            return {0};
        }
        case NodeType::Slider: {
            SliderNode* nodeData =
                GetNodeData<SliderNode>(*node).value_or(nullptr);
            if (nodeData) {
                return std::to_string(nodeData->value);
            }
            return {0};
        }
        case NodeType::OneMinusX: {
            auto inputNode =
                treeNode->GetInputs().begin()->second.front().nodeID;
            return "1.0 - " +
                   GenerateNodeShaderCode(tree.nodes[inputNode].get());
        }
        case NodeType::Multiply: {
            auto inputIter = treeNode->GetInputs().begin();
            auto input1NodeId = inputIter->second.front().nodeID;
            auto input2NodeId = (++inputIter)->second.front().nodeID;

            return GenerateNodeShaderCode(tree.nodes[input1NodeId].get()) +
                   " * " +
                   GenerateNodeShaderCode(tree.nodes[input2NodeId].get());
        }
        default:
            TraceLog(LOG_WARNING,
                     "[ Generate Node Code ] Unsupported node type.");
            return {0};
        }
    };

    auto ExtractConnections =
        [](TreeNode* node) -> std::vector<std::pair<int, Connection>> {
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
            TraceLog(LOG_WARNING, "[ProcessTreeNode] Invalid tree node.");
            return "ERROR";
        }

        std::string accumulatedCode;

        // Precompute connections
        std::vector<std::pair<int, Connection>> allConnections =
            ExtractConnections(treeNode);

        for (const auto& [pinId, conn] : allConnections) {
            if ((nodePinId.Get() != -1) && (pinId != nodePinId.Get()))
                continue;

            auto it = tree.nodes.find(conn.nodeID);
            if (it == tree.nodes.end()) {
                TraceLog(LOG_WARNING,
                         "[ProcessTreeNode] Node with ID %d not found.",
                         conn.nodeID);
                continue;
            }

            accumulatedCode += GenerateNodeShaderCode(it->second.get());
            ProcessTreeNode(it->second.get(), ed::PinId(-1), "");
        }

        if (accumulatedCode.empty())
            accumulatedCode = defaultCode;

        return accumulatedCode;
    };

    Node* rootNode = materialNodeSystem.FindNode(tree.root->GetId());
    if (!rootNode)
        return { 0 };

    std::stringstream shaderStream;

    shaderStream << "#define NORMAL_SUPPORT\n\n";

    shaderStream << "vec4 calcDiffuseMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(0).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcNormalMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(1).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcRoughnessMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(2).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcAmbientOcclusionMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(3).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcHeightMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(4).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcMetallicMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(5).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    shaderStream << "vec4 calcEmissionMap(vec4 textureRGBA) {\n  return ";
    shaderStream << ProcessTreeNode(
        tree.root,
        rootNode->Inputs.at(6).ID,
        "textureRGBA");
    shaderStream << ";\n}\n\n";

    std::ifstream fragStream("Engine/Lighting/shaders/lighting_fragment.glsl");
    std::string defaultShaderCode((std::istreambuf_iterator<char>(fragStream)),
                                  (std::istreambuf_iterator<char>()));

    std::ifstream vertexStream("Engine/Lighting/shaders/lighting_vertex.glsl");
    std::string lightingShaderVertexCode(
        (std::istreambuf_iterator<char>(vertexStream)),
        (std::istreambuf_iterator<char>()));

    static const std::string placeholder = "// [ INSERT GENERATED CODE BELOW ]";

    size_t pos = defaultShaderCode.find(placeholder);
    if (pos != std::string::npos) {
        defaultShaderCode.replace(pos, placeholder.length(),
                                  shaderStream.str());
    }

    std::function<Node*(TreeNode*, std::function<bool(const Node*)>)> findNode =
        [&](TreeNode* treeNode, std::function<bool(const Node*)> predicate) -> Node* {
            if (!treeNode)
                return nullptr;

            Node* currentNode = materialNodeSystem.FindNode(treeNode->GetId());
            if (currentNode && predicate(currentNode))
                return currentNode;

            // Traverse all input connections to find a matching node.
            for (const auto& [pinId, connections] : treeNode->GetInputs()) {
                for (const auto& conn : connections) {
                    auto it = tree.nodes.find(conn.nodeID);
                    if (it != tree.nodes.end()) {
                        Node* foundNode = findNode(it->second.get(), predicate);
                        if (foundNode)
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

        switch (pinIndex) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
                node = findNode(connectedTreeNode, [](const Node* n) {
                    return n->type == NodeType::Texture;
                });
                if (!node)
                    continue;

                if (TextureNode* textureData = GetNodeData<TextureNode>(*node).value_or(nullptr)) {
                    switch (pinIndex) {
                        case 0:
                            selectedEntity->surfaceMaterial.albedoTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.albedoTexturePath = textureData->texturePath;
                            break;
                        case 1:
                            selectedEntity->surfaceMaterial.normalTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.normalTexturePath = textureData->texturePath;
                            break;
                        case 2:
                            selectedEntity->surfaceMaterial.roughnessTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.roughnessTexturePath = textureData->texturePath;
                            break;
                        case 3:
                            selectedEntity->surfaceMaterial.aoTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.aoTexturePath = textureData->texturePath;
                            break;
                        case 4:
                            selectedEntity->surfaceMaterial.heightTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.heightTexturePath = textureData->texturePath;
                            break;
                        case 5:
                            selectedEntity->surfaceMaterial.metallicTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.metallicTexturePath = textureData->texturePath;
                            break;
                        case 6:
                            selectedEntity->surfaceMaterial.emissiveTexture     = textureData->texture;
                            selectedEntity->surfaceMaterial.emissiveTexturePath = textureData->texturePath;
                            break;
                        default:
                            break;
                    }
                }
                break;
            case 7:
                node = findNode(connectedTreeNode, [](const Node* n) {
                    return n->type == NodeType::Vector2;
                });
                if (!node)
                    continue;

                if (Vector2Node* vec2NodeData = GetNodeData<Vector2Node>(*node).value_or(nullptr)) {
                    selectedEntity->surfaceMaterial.tiling[0] = vec2NodeData->vec[0];
                    selectedEntity->surfaceMaterial.tiling[1] = vec2NodeData->vec[1];
                }
                break;
            default:
                break;
        }
    }


    return LoadShaderFromMemory(lightingShaderVertexCode.c_str(),
                                defaultShaderCode.c_str());
}