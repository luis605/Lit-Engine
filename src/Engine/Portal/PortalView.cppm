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
    // Opening and edge (PortalLook): x shape (0 rect, 1 ellipse, 2 rounded), y corner radius (m), z feather (px), w dissolve (0..1).
    glm::vec4 shape{0.0f, 0.0f, 1.0f, 0.0f};
    // x rim glow strength (exposed HDR), y rim width (m), z refraction (px), w chromatic split (px).
    glm::vec4 edge{0.0f};
    // rgb rim colour, w Fresnel strength.
    glm::vec4 rim{0.0f};
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
    glm::vec3 lightTint{1.0f};  // cell lighting grade (sun and ambient colour), from the cell tint
    glm::vec3 cellMin{-1.0e9f};  // bounds of the view's cell (shared sun cascades test their coverage against them)
    glm::vec3 cellMax{1.0e9f};
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
    bool sunCascades = false;     // the view is sunlit and shades with cascades
    bool sharedCascades = false;  // ... reusing the set of another view (no cascade rendering of its own)
};
