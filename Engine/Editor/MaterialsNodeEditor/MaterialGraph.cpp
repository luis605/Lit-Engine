class TreeNode {
private:
    ed::NodeId id;

    // pinID -> vector of in/out connections
    std::unordered_map<int, std::vector<Connection>> inputs;
    std::unordered_map<int, std::vector<Connection>> outputs;
    int depth;

public:
    TreeNode(ed::NodeId nodeId) : id(nodeId), depth(0) {}

    void AddInput(int pinId, const Connection& connection) {
        inputs[pinId].push_back(connection);
    }

    void AddOutput(int pinId, const Connection& connection) {
        outputs[pinId].push_back(connection);
    }

    void SetDepth(int newDepth) { depth = newDepth; }
    int GetDepth() const { return depth; }
    ed::NodeId GetId() const { return id; }

    const auto& GetInputs() const { return inputs; }
    const auto& GetOutputs() const { return outputs; }
};

class MaterialTree {
private:
    TreeNode* FindRoot() {
        if (!root) {
            throw std::runtime_error("No root node found in the material tree.");
        }
        return root;
    }

public:
    std::unordered_map<int, std::unique_ptr<TreeNode>> nodes;
    TreeNode* root = nullptr;

public:
    void ForwardTraversal(TreeNode* node, std::unordered_set<int>& visited,
                          const std::function<void(TreeNode*, int)>& visitor) {
        if (!node || visited.count(node->GetId().Get())) return;

        visited.insert(node->GetId().Get());
        visitor(node, node->GetDepth());

        // Traverse upstream: iterate over node inputs
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

    void BackwardTraversal(TreeNode* node, std::unordered_set<int>& visited,
                          const std::function<void(TreeNode*, int)>& visitor) {
        if (!node || visited.count(node->GetId().Get())) return;

        visited.insert(node->GetId().Get());
        visitor(node, node->GetDepth());

        // Traverse downstream: iterate over node outputs
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

    MaterialTree() {}

    void AddNode(const Node& node) {
        auto& newNode = nodes[node.ID.Get()];
        newNode = std::make_unique<TreeNode>(node.ID);

        if (node.isRoot) {
            if (root) {
                throw std::runtime_error("Multiple nodes marked as root.");
            }
            root = newNode.get();
        }
    }

    void AddLink(const Link& link, int startNodeId, int endNodeId) {
        auto& startNode = nodes[startNodeId];
        auto& endNode = nodes[endNodeId];

        startNode->AddOutput(link.StartPinID.Get(),
            Connection(endNodeId, link.EndPinID.Get()));

        endNode->AddInput(link.EndPinID.Get(),
            Connection(startNodeId, link.StartPinID.Get()));
    }

    void Initialize() {
        root = FindRoot();
        if (!root) {
            throw std::runtime_error("No root node found in the material tree");
        }

        // Initialize depths
        std::unordered_set<int> visited;
        ForwardTraversal(root, visited, [](TreeNode* node, int depth) {
            node->SetDepth(depth);
        });
    }

    static MaterialTree BuildTree(const std::vector<Node>& nodes,
                                const std::vector<Link>& links) {
        MaterialTree tree;

        for (const auto& node : nodes) {
            tree.AddNode(node);
        }

        for (const auto& link : links) {
            int startNodeId = FindNodeByPinID(nodes, link.StartPinID.Get());
            int endNodeId = FindNodeByPinID(nodes, link.EndPinID.Get());

            if (startNodeId != -1 && endNodeId != -1) {
                tree.AddLink(link, startNodeId, endNodeId);
            }
        }

        tree.Initialize();
        return tree;
    }

    void Print() const {
        if (!root) {
            std::cerr << "Tree has no root node. Cannot print." << std::endl;
            return;
        }

        std::cout << "Node " << root->GetId().Get() << std::endl;

        std::function<void(TreeNode*, const std::string&, bool)> PrintNode =
            [&](TreeNode* node, const std::string& prefix, bool isLast) {
                if (!node) return;

                // Precompute connections
                const auto& inputs = node->GetInputs();
                std::vector<std::pair<int, Connection>> allConnections;
                for (const auto& [pinId, connections] : inputs) {
                    for (const auto& conn : connections) {
                        allConnections.emplace_back(pinId, conn);
                    }
                }

                for (size_t i = 0; i < allConnections.size(); ++i) {
                    const auto& [pinId, conn] = allConnections[i];
                    bool isLastConnection = (i == allConnections.size() - 1);

                    // Print prefix and connection
                    std::cout << prefix << (isLastConnection ? "└─ " : "├─ ")
                            << "(Pin: " << pinId << ") -> Node "
                            << conn.nodeID << " (Pin: " << conn.pinID << ")"
                            << std::endl;

                    // Recursive call
                    auto it = nodes.find(conn.nodeID);
                    if (it != nodes.end()) {
                        PrintNode(it->second.get(), prefix + (isLastConnection ? "   " : "│  "), isLastConnection);
                    } else {
                        std::cerr << "Warning: Node " << conn.nodeID << " not found." << std::endl;
                    }
                }
            };

        PrintNode(root, "", false);
    }
};