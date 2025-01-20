const char* multiplyColorTextureShaderNode = R"(
vec4 multiplyColorTextureShaderNode(vec4 texColor, float multiplier) {
    return vec4(texColor.rgb * multiplier, texColor.a);
}
)";

const char* multiplyFloatShaderNode = R"(
float multiplyFloat(float value, float multiplier) {
    return value * multiplier;
}
)";

std::string GenerateMaterialShader() {
    MaterialTree tree = MaterialTree::BuildTree(materialNodeSystem.m_Nodes, materialNodeSystem.m_Links);

    if (!tree.root) {
        TraceLog(LOG_WARNING, "[ Generate Material Shader ] Material tree has no valid root node.");
        return "";
    }

    std::function<std::string(TreeNode*)> GenerateNodeShaderCode = [&](TreeNode* treeNode) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_WARNING, "[GenerateNodeShaderCode] Null tree node encountered.");
            return "";
        }

        Node* node = materialNodeSystem.FindNode(treeNode->GetId());
        if (!node) {
            TraceLog(LOG_WARNING, "[GenerateNodeShaderCode] Null node encountered.");
            return "";
        }

        switch (node->type) {
            case NodeType::Texture: {
                return "texture0";
            }
            case NodeType::Color: {
                ColorNode* nodeData = GetNodeData<ColorNode>(*node).value_or(nullptr);
                if (nodeData) {
                    return std::string("vec4(" + std::to_string(nodeData->color.Value.x) + ", " + std::to_string(nodeData->color.Value.x) + ", " + std::to_string(nodeData->color.Value.x) + ", " + std::to_string(nodeData->color.Value.w) + ")");
                }
                return "";
            }
            case NodeType::Slider: {
                SliderNode* nodeData = GetNodeData<SliderNode>(*node).value_or(nullptr);
                if (nodeData) {
                    return std::to_string(nodeData->value);
                }
                return "";
            }
            case NodeType::OneMinusX: {
                auto inputNode = treeNode->GetInputs().begin()->second.front().nodeID;
                return "1.0 - " + GenerateNodeShaderCode(tree.nodes[inputNode].get());
            }
            case NodeType::Multiply: {
                auto inputIter = treeNode->GetInputs().begin();
                auto input1NodeId = inputIter->second.front().nodeID;
                auto input2NodeId = (++inputIter)->second.front().nodeID;

                return "multiplyFloat(" +
                    GenerateNodeShaderCode(tree.nodes[input1NodeId].get()) + ", " +
                    GenerateNodeShaderCode(tree.nodes[input2NodeId].get()) + ")";
            }
            default:
                TraceLog(LOG_WARNING, "[ Generate Node Code ] Unsupported node type.");
                return "";
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

    std::function<std::string(TreeNode*, const ed::PinId&)> ProcessTreeNode = [&](TreeNode* treeNode, const ed::PinId& nodePinId) -> std::string {
        if (!treeNode) {
            TraceLog(LOG_WARNING, "[ProcessTreeNode] Invalid tree node.");
            return "ERROR";
        }

        std::string accumulatedCode;

        // Precompute connections
        std::vector<std::pair<int, Connection>> allConnections = ExtractConnections(treeNode);

        for (const auto& [pinId, conn] : allConnections) {
            if ((nodePinId.Get() != -1) && (pinId != nodePinId.Get())) continue;

            auto it = tree.nodes.find(conn.nodeID);
            if (it == tree.nodes.end()) {
                TraceLog(LOG_WARNING, "[ProcessTreeNode] Node with ID %d not found.", conn.nodeID);
                continue;
            }

            accumulatedCode += GenerateNodeShaderCode(it->second.get());
            ProcessTreeNode(it->second.get(), ed::PinId(-1));
        }

        return accumulatedCode;
    };

    // Final assembly of shader code
    std::stringstream shaderStream;

    // Clear Coat
    shaderStream << "vec4 diffuseMap() {\nreturn ";
    shaderStream << ProcessTreeNode(tree.root, materialNodeSystem.FindNode(tree.root->GetId())->Inputs.at(0).ID);
    shaderStream << ";\n}\n\n";

    shaderStream << "float magicNumber() {\nreturn ";
    shaderStream << ProcessTreeNode(tree.root, materialNodeSystem.FindNode(tree.root->GetId())->Inputs.back().ID);
    shaderStream << ";\n}\n\n";

    return shaderStream.str();
}