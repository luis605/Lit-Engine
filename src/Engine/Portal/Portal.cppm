module;

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Engine.Portal;

import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.Portal.view;

export struct PortalCellParams {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    glm::vec3 tint{0.1f};
    float sun = 1.0f;
    float ambient = 1.0f;
    std::map<std::string, float> scalars;
};

export struct PortalCell {
    std::string name;
    glm::vec3 min{-1.0e9f};
    glm::vec3 max{1.0e9f};
    PortalCellParams params;
};

export struct PortalEnd {
    std::string cell;
    glm::vec3 position{0.0f};
    glm::vec3 eulerDegrees{0.0f};
    glm::vec2 halfSize{1.0f, 1.0f};
};

export struct Portal {
    std::string name;
    int cell = -1;
    int partner = -1;
    glm::mat4 pose{1.0f};
    glm::vec2 half{1.0f, 1.0f};
    glm::mat4 transform{1.0f};
    float twist = 0.0f;
    [[nodiscard]] glm::vec3 center() const { return glm::vec3(pose[3]); }
    [[nodiscard]] glm::vec3 normal() const { return glm::vec3(pose[2]); }
    [[nodiscard]] glm::vec3 right() const { return glm::vec3(pose[0]); }
    [[nodiscard]] glm::vec3 up() const { return glm::vec3(pose[1]); }
};

export struct PortalSettings {
    int maxDepth = 3;
    int maxViews = 24;
    float blendDistance = 2.0f;
    float minPixels = 6.0f;
    bool occlusion = true;
};

export struct PortalRayHit {
    std::optional<RayHit> hit;
    glm::vec3 point{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float distance = 0.0f;
    int cell = -1;
    int crossings = 0;
};

export struct PortalOverlap {
    EntityHandle entity;
    int crossings = 0;
};

export struct PortalSceneKit {
    uint32_t cube = 0;
    uint32_t sphere = 0;
    uint32_t floorShader = 0;
    std::function<uint32_t(const glm::vec3& color, const glm::vec3& emissive, float emissiveIntensity, float roughness)> material;
};

export using PortalCrossHook = std::function<void(World&, EntityHandle, const glm::mat4&)>;

export using PortalBlockedFn = std::function<bool(const glm::vec3&, const glm::vec3&)>;

export [[nodiscard]] glm::mat4 portalPose(const glm::vec3& position, const glm::vec3& eulerDegrees);
export [[nodiscard]] glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::mat4& view, const glm::vec4& worldPlane);
export [[nodiscard]] std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProjection);
export void applyTransformToCamera(Camera& camera, const glm::mat4& transform);

export class PortalSystem {
  public:
    void clear();
    int addCell(const PortalCell& cell);
    int addPair(const std::string& name, const PortalEnd& a, const PortalEnd& b, float twistDegrees = 0.0f);
    bool loadJson(const std::string& path);
    bool parseJson(std::string_view text);
    [[nodiscard]] std::string toJson() const;

    [[nodiscard]] bool active() const { return !m_portals.empty(); }
    [[nodiscard]] const std::vector<PortalCell>& cells() const { return m_cells; }
    [[nodiscard]] PortalCellParams& cellParams(int cell) { return m_cells[static_cast<size_t>(cell)].params; }
    [[nodiscard]] const std::vector<Portal>& portals() const { return m_portals; }
    [[nodiscard]] const std::vector<int>& portalsOf(int cell) const;
    [[nodiscard]] PortalSettings& settings() { return m_settings; }
    [[nodiscard]] const PortalSettings& settings() const { return m_settings; }
    [[nodiscard]] int findCell(const std::string& name) const;
    [[nodiscard]] int cellAt(const glm::vec3& position) const;
    [[nodiscard]] int resolveCell(int cell, const glm::vec3& position) const;
    [[nodiscard]] int cameraCell() const { return m_cameraCell; }
    void setCameraCell(int cell);

    [[nodiscard]] PortalFrame buildFrame(const Camera& camera, float viewportHeight, const PortalBlockedFn& blocked = {}) const;
    int step(Camera& camera);
    int stepPoint(glm::vec3& position, int& cell, const glm::vec3& previous, bool upright = false) const;
    [[nodiscard]] std::optional<std::pair<int, float>> firstCrossing(int cell, const glm::vec3& origin, const glm::vec3& direction, float maxT) const;
    void setCrossHook(PortalCrossHook hook) { m_crossHook = std::move(hook); }

    [[nodiscard]] PortalCellParams sampleParams(int cell, const glm::vec3& position) const;
    [[nodiscard]] PortalRayHit raycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell = -1, int maxCrossings = 8) const;
    [[nodiscard]] std::vector<PortalOverlap> overlapSphere(World& world, const glm::vec3& center, float radius, int cell = -1) const;

    void track(World& world, EntityHandle entity);
    void untrack(World& world, EntityHandle entity);
    void updateObjects(World& world);
    [[nodiscard]] EntityHandle ghostOf(EntityHandle entity) const;
    [[nodiscard]] int objectCell(EntityHandle entity) const;
    [[nodiscard]] bool isGhost(EntityHandle entity) const;

  private:
    struct Tracked {
        EntityHandle entity;
        EntityHandle ghost;
        glm::vec3 previous{0.0f};
        int cell = -1;
    };
    std::vector<PortalCell> m_cells;
    std::vector<Portal> m_portals;
    std::vector<std::vector<int>> m_cellPortals;
    PortalSettings m_settings;
    int m_cameraCell = -1;
    bool m_hasNear = false;
    glm::vec3 m_previousNear{0.0f};
    std::vector<Tracked> m_tracked;
    PortalCrossHook m_crossHook;
};

export enum class PortalTestScene { Loop, Torture };

export void buildPortalScene(PortalSystem& portals, World& world, const PortalSceneKit& kit, PortalTestScene which);
export [[nodiscard]] glm::vec3 portalSceneStart(PortalTestScene which, float& yawDegrees, float& pitchDegrees);
