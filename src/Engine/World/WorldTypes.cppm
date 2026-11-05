module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Engine.World:Types;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

export struct CameraComponent {
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

export struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

export struct RayHit {
    EntityHandle entity;
    float distance = 0.0f;
};

export struct TimeState {
    float deltaTime = 0.0f;
    float unscaledDeltaTime = 0.0f;
    double elapsed = 0.0;
    double unscaledElapsed = 0.0;
    uint64_t frame = 0;
    float timeScale = 1.0f;
    bool paused = false;
};

export struct LightComponent {
    enum class Type : uint32_t { Directional, Point, Spot };
    Type type = Type::Point;
    glm::vec3 color{1.0f};
    float intensity = 1.0e6f;
    float range = 100.0f;
    float specular = 1.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 30.0f;
    float sourceRadius = 0.0f;
    float sourceLength = 0.0f;
};

export struct ReflectionProbeComponent {
    glm::vec3 halfExtents{5.0f};
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    uint32_t resolution = 128;
};

export struct PlanarReflectionComponent {
    glm::vec2 halfSize{10.0f};
};

export struct LightSet {
    std::array<glm::vec4, 2> directional{};
    std::vector<glm::vec4> packed;
    uint32_t count = 0;
};

export struct EntityDescription {
    std::string name;
    std::string tag;
    uint32_t layer = 1;
    uint32_t mesh = NO_MESH;
    bool visible = true;
    EntityHandle parent;
    size_t childCount = 0;
    std::vector<std::pair<std::string, std::string>> components;
    std::vector<std::pair<std::string, std::string>> scripts;
};

export struct EntityMoved {
    EntityHandle from;
    EntityHandle to;
};

export struct EntityCreated {
    EntityHandle entity;
};

export struct EntityDestroyed {
    EntityHandle entity;
};

export struct EntityReparented {
    EntityHandle entity;
    EntityHandle oldParent;
    EntityHandle newParent;
};

export struct EntityDesc {
    std::string name;
    uint32_t mesh = NO_MESH;
    uint32_t material = 0;
    uint32_t shader = 0;
    float alpha = 1.0f;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    EntityHandle parent = NULL_ENTITY;
};

export struct PrefabNode {
    std::string name;
    uint32_t mesh = 0;
    uint32_t material = 0;
    uint32_t shader = 0;
    float alpha = 1.0f;
    bool visible = true;
    uint32_t layer = 1;
    uint32_t renderFlags = 0;
    std::string tag;
    glm::mat4 local{1.0f};
    int parent = -1;
    uint32_t sourceIndex = INVALID_ENTITY;
    uint32_t sourceGeneration = 0;
    std::vector<std::pair<std::string, std::string>> components;
    std::vector<std::pair<std::string, std::string>> scripts;
};

export struct Prefab {
    std::vector<PrefabNode> nodes;
    [[nodiscard]] bool empty() const { return nodes.empty(); }
};

export class TextTable {
  public:
    TextTable() : m_texts(1) {}

    [[nodiscard]] inline const std::string& get(Entity index) const { return m_texts[index < m_slotOf.size() ? m_slotOf[index] : 0]; }
    [[nodiscard]] inline size_t size() const { return m_slotOf.size(); }
    inline void reserve(size_t count) { m_slotOf.reserve(count); }
    inline void grow() { m_slotOf.push_back(0); }

    inline void set(Entity index, std::string_view text) {
        if (text.empty()) {
            erase(index);
            return;
        }
        uint32_t& slot = m_slotOf[index];
        if (slot == 0) {
            if (m_freeSlots.empty()) {
                slot = static_cast<uint32_t>(m_texts.size());
                m_texts.emplace_back();
            } else {
                slot = m_freeSlots.back();
                m_freeSlots.pop_back();
            }
        }
        m_texts[slot].assign(text);
    }

    inline void erase(Entity index) {
        uint32_t& slot = m_slotOf[index];
        if (slot == 0) return;
        m_texts[slot].clear();
        m_freeSlots.push_back(slot);
        slot = 0;
    }

    inline void move(Entity from, Entity to) {
        std::swap(m_slotOf[from], m_slotOf[to]);
        erase(from);
    }

    inline void resize(size_t count) {
        for (size_t index = count; index < m_slotOf.size(); ++index) erase(static_cast<Entity>(index));
        m_slotOf.resize(count, 0);
    }

    inline void clear() {
        m_slotOf.clear();
        m_freeSlots.clear();
        m_texts.assign(1, std::string());
    }

  private:
    std::vector<uint32_t> m_slotOf;
    std::vector<std::string> m_texts;
    std::vector<uint32_t> m_freeSlots;
};

export struct WorldSnapshot {
    std::vector<TransformComponent> transforms;
    std::vector<HierarchyComponent> hierarchies;
    std::vector<RenderableComponent> renderables;
    std::vector<Entity> firstChild;
    std::vector<Entity> nextSibling;
    std::vector<Entity> prevSibling;
    std::vector<uint8_t> alive;
    std::vector<uint8_t> visible;
    std::vector<uint32_t> generation;
    std::vector<uint32_t> mesh;
    std::vector<uint32_t> layer;
    TextTable tags;
    TextTable names;
    std::vector<Entity> freeList;
    Entity firstRoot = INVALID_ENTITY;
    size_t aliveCount = 0;
    std::string extras;
    bool valid = false;
};

export struct SpatialEntry {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    uint64_t cell = 0;
    uint32_t slot = 0;
    uint8_t state = 0;
    uint8_t animated = 0;
};
