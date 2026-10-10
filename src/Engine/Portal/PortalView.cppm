module;

#include <array>
#include <cstdint>
#include <string>
#include <vector>

export module Engine.Portal.view;

import Engine.glm;

export struct PortalSurface {
    glm::vec3 center{0.0f};
    glm::vec3 axisU{1.0f, 0.0f, 0.0f};
    glm::vec3 axisV{0.0f, 1.0f, 0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec3 fallback{0.05f};
    int child = -1;
};

export struct PortalViewNode {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f};
    std::array<glm::vec4, 6> planes{};
    uint64_t key = 0;
    int depth = 0;
    int cell = -1;
    float sunScale = 1.0f;
    float ambientScale = 1.0f;
    bool masked = false;
    glm::vec3 entryCenter{0.0f};
    glm::vec3 entryAxisU{1.0f, 0.0f, 0.0f};
    glm::vec3 entryAxisV{0.0f, 1.0f, 0.0f};
    glm::vec3 entryNormal{0.0f, 0.0f, 1.0f};
    std::string label;
    std::vector<PortalSurface> surfaces;
};

export struct PortalFrame {
    std::vector<PortalViewNode> nodes;
    [[nodiscard]] bool empty() const { return nodes.size() < 2; }
};

export struct PortalViewStats {
    std::string label;
    int depth = 0;
    uint32_t drawn = 0;
    double gpuMilliseconds = 0.0;
};
