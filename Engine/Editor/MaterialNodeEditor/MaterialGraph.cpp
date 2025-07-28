/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "MaterialGraph.hpp"
#include "Helpers.hpp"
#include <iostream>
#include <stdexcept>
#include <unordered_set>

void TreeNode::AddInput(int pinId, const Connection& connection) {
    inputs[pinId].push_back(connection);
}

void TreeNode::AddOutput(int pinId, const Connection& connection) {
    outputs[pinId].push_back(connection);
}

void TreeNode::SetDepth(int newDepth) { depth = newDepth; }
int TreeNode::GetDepth() const { return depth; }
ed::NodeId TreeNode::GetId() const { return id; }

const std::unordered_map<int, std::vector<Connection>>& TreeNode::GetInputs() const { return inputs; }
const std::unordered_map<int, std::vector<Connection>>& TreeNode::GetOutputs() const { return outputs; }

TreeNode* MaterialTree::FindRoot() {
    if (root) {
        return root;
    }
    for (auto& [id, node] : nodes) {
        if (node->GetOutputs().empty()) {
            root = node.get();
            return root;
        }
    }
    return nullptr;
}

void MaterialTree::ForwardTraversal(
    TreeNode* node, std::unordered_set<int>& visited,
    const std::function<void(TreeNode*, int)>& visitor) {
    if (!node || visited.count(node->GetId().Get()))
        return;

    visited.insert(node->GetId().Get());
    visitor(node, node->GetDepth());

    for (const auto& [pinId, connections] : node->GetInputs()) {
        for (const auto& conn : connections) {
            auto it = nodes.find(conn.nodeID);
            if (it != nodes.end()) {
                TreeNode* parentNode = it->second.get();
                ForwardTraversal(parentNode, visited, visitor);
            }
        }
    }
}

void MaterialTree::BackwardTraversal(
    TreeNode* node, std::unordered_set<int>& visited,
    const std::function<void(TreeNode*, int)>& visitor) {
    if (!node || visited.count(node->GetId().Get()))
        return;

    visited.insert(node->GetId().Get());
    visitor(node, node->GetDepth());

    for (const auto& [pinId, connections] : node->GetOutputs()) {
        for (const auto& conn : connections) {
            auto it = nodes.find(conn.nodeID);
            if (it != nodes.end()) {
                TreeNode* childNode = it->second.get();
                BackwardTraversal(childNode, visited, visitor);
            }
        }
    }
}

void MaterialTree::AddNode(const Node& node) {
    auto& newNode = nodes[node.m_id.Get()];
    newNode = std::make_unique<TreeNode>(node.m_id);

    if (node.m_isRoot) {
        if (root) {
            throw std::runtime_error("Multiple nodes marked as root.");
        }
        root = newNode.get();
    }
}

void MaterialTree::AddLink(const Link& link, int startNodeId, int endNodeId) {
    auto itStart = nodes.find(startNodeId);
    auto itEnd = nodes.find(endNodeId);

    if (itStart == nodes.end() || itEnd == nodes.end()) {
        return;
    }

    auto& startNode = itStart->second;
    auto& endNode = itEnd->second;

    startNode->AddOutput(link.m_startPinId.Get(),
                         Connection(endNodeId, link.m_endPinId.Get()));

    endNode->AddInput(link.m_endPinId.Get(),
                      Connection(startNodeId, link.m_startPinId.Get()));
}

void MaterialTree::Initialize() {
    root = FindRoot();
    if (!root) {
        throw std::runtime_error("No root node found in the material tree");
    }

    std::unordered_set<int> visited;
    ForwardTraversal(root, visited,
                     [](TreeNode* node, int depth) { node->SetDepth(0); });
}

MaterialTree MaterialTree::BuildTree(const std::unordered_map<ed::NodeId, Node>& nodes,
                                            const std::unordered_map<ed::LinkId, Link>& links) {
    MaterialTree tree;

    for (const auto& [nodeId, node] : nodes) {
        tree.AddNode(node);
    }

    std::unordered_map<ed::PinId, ed::NodeId> pinToNodeMap;
    for (const auto& [nodeId, node] : nodes) {
        for (const auto& pinId : node.m_inputs) {
            pinToNodeMap[pinId] = nodeId;
        }
        for (const auto& pinId : node.m_outputs) {
            pinToNodeMap[pinId] = nodeId;
        }
    }

    for (const auto& [linkId, link] : links) {
        auto startNodeIt = pinToNodeMap.find(link.m_startPinId);
        auto endNodeIt = pinToNodeMap.find(link.m_endPinId);

        if (startNodeIt != pinToNodeMap.end() && endNodeIt != pinToNodeMap.end()) {
            int startNodeId = startNodeIt->second.Get();
            int endNodeId = endNodeIt->second.Get();
            tree.AddLink(link, startNodeId, endNodeId);
        }
    }

    tree.Initialize();
    return tree;
}

void MaterialTree::Print() const {
    if (!root) {
        std::cerr << "Tree has no root node. Cannot print." << std::endl;
        return;
    }

    std::cout << "Root Node " << root->GetId().Get() << std::endl;

    std::function<void(TreeNode*, const std::string&)> PrintNode =
        [&](TreeNode* node, const std::string& prefix) {
            if (!node) return;

            std::vector<std::pair<int, Connection>> allConnections;
            const auto& outputs = node->GetOutputs();
            for (const auto& [pinId, connections] : outputs) {
                for (const auto& conn : connections) {
                    allConnections.emplace_back(pinId, conn);
                }
            }

            if (allConnections.empty()) return;

            for (size_t i = 0; i < allConnections.size(); ++i) {
                const auto& [pinId, conn] = allConnections[i];
                bool isLast = (i == allConnections.size() - 1);

                std::cout << prefix << (isLast ? "└─ " : "├─ ")
                          << "Pin " << pinId << " connects to Node " << conn.nodeID
                          << " (Pin: " << conn.pinID << ")" << std::endl;

                auto childIt = nodes.find(conn.nodeID);
                if (childIt != nodes.end()) {
                    PrintNode(childIt->second.get(), prefix + (isLast ? "    " : "│   "));
                }
            }
        };

    PrintNode(root, "");
}