/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_GRAPH_HPP
#define MATERIAL_GRAPH_HPP

#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <NodeEditor/examples/application/include/application.h>
#include <NodeEditor/imgui_canvas.h>
#include <NodeEditor/imgui_node_editor.h>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

class TreeNode {
  private:
    ed::NodeId id;

    // pinID -> vector of in/out connections
    std::unordered_map<int, std::vector<Connection>> inputs;
    std::unordered_map<int, std::vector<Connection>> outputs;
    int depth;

  public:
    TreeNode(ed::NodeId nodeId) : id(nodeId), depth(0) {}

    void AddInput(int pinId, const Connection& connection);
    void AddOutput(int pinId, const Connection& connection);
    void SetDepth(int newDepth);
    int GetDepth() const;
    ed::NodeId GetId() const;
    const std::unordered_map<int, std::vector<Connection>>& GetInputs() const;
    const std::unordered_map<int, std::vector<Connection>>& GetOutputs() const;
};

class MaterialTree {
  public:
    std::unordered_map<int, std::unique_ptr<TreeNode>> nodes;
    TreeNode* root = nullptr;

  private:
    TreeNode* FindRoot();

  public:
    MaterialTree() {}
    void ForwardTraversal(TreeNode* node, std::unordered_set<int>& visited,
                          const std::function<void(TreeNode*, int)>& visitor);
    void BackwardTraversal(TreeNode* node, std::unordered_set<int>& visited,
                           const std::function<void(TreeNode*, int)>& visitor);
    void AddNode(const Node& node);
    void AddLink(const Link& link, int startNodeId, int endNodeId);
    void Initialize();
    static MaterialTree BuildTree(const std::vector<Node>& nodes,
                                  const std::vector<Link>& links);
    void Print() const;
};

#endif // MATERIAL_GRAPH_HPP