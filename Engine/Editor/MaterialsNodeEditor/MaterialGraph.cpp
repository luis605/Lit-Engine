std::unordered_map<int, std::vector<Connection>> buildGraph(const std::vector<Node>& nodes, const std::vector<Link>& links) {
    std::unordered_map<int, std::vector<Connection>> adjacencyList;

    // First add all nodes to ensure isolated nodes are included
    for (const auto& node : nodes) {
        adjacencyList[node.ID.Get()];  // Create empty vector for each node
    }

    // Populate adjacency list using links
    for (const auto& link : links) {
        int startNodeID = findNodeByPinID(nodes, link.StartPinID.Get());
        int endNodeID = findNodeByPinID(nodes, link.EndPinID.Get());

        if (startNodeID != -1 && endNodeID != -1) {
            // Store both the target node ID and the connected pin ID
            adjacencyList[startNodeID].push_back(Connection(endNodeID, link.EndPinID.Get()));
        }
    }
    return adjacencyList;
}

std::vector<int> topologicalSort(const std::unordered_map<int, std::vector<Connection>>& graph) {
    std::vector<int> sortedNodes;
    std::unordered_map<int, int> inDegree;

    // Initialize in-degree for all nodes
    for (const auto& [node, neighbors] : graph) {
        inDegree[node] = 0;
        for (const auto& conn : neighbors) {
            inDegree[conn.nodeID] = 0;
        }
    }

    // Calculate in-degree for each node
    for (const auto& [node, neighbors] : graph) {
        for (const auto& conn : neighbors) {
            inDegree[conn.nodeID]++;
        }
    }

    // Add nodes with no dependencies to queue
    std::queue<int> q;
    for (const auto& [node, degree] : inDegree) {
        if (degree == 0) {
            q.push(node);
        }
    }

    // Process nodes in topological order
    while (!q.empty()) {
        int current = q.front();
        q.pop();
        sortedNodes.push_back(current);

        if (graph.find(current) != graph.end()) {
            for (const auto& conn : graph.at(current)) {
                inDegree[conn.nodeID]--;
                if (inDegree[conn.nodeID] == 0) {
                    q.push(conn.nodeID);
                }
            }
        }
    }

    if (sortedNodes.size() != inDegree.size()) {
        return std::vector<int>();
    }

    return sortedNodes;
}

void printGraph(const std::unordered_map<int, std::vector<Connection>>& graph) {
    for (const auto& [node, neighbors] : graph) {
        std::cout << "Node " << node << " -> [";
        for (size_t i = 0; i < neighbors.size(); ++i) {
            std::cout << "Node:" << neighbors[i].nodeID
                      << "(Pin:" << neighbors[i].pinID << ")";
            if (i != neighbors.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;
    }
}
