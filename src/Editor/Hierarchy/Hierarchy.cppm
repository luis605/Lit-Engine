module;

#include <cstdint>
#include <unordered_set>
#include <vector>

export module Editor.hierarchy;

import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

export struct HierarchyAction {
    enum class Kind { None, Select, Reparent };
    Kind kind = Kind::None;
    EntityHandle entity;
    EntityHandle newParent;
    bool consumedMouse = false;
};

export class Hierarchy {
  public:
    HierarchyAction update(Engine& engine, EntityHandle& selected, bool inputEnabled = true);
    void reset() {
        m_expanded.clear();
        m_offset = 0;
        m_lastSelected = NULL_ENTITY;
    }

  private:
    struct Row {
        EntityHandle entity;
        int depth;
        bool hasChildren;
    };

    void collectRows(World& world, size_t limit, std::vector<Row>& rows) const;
    void reveal(World& world, EntityHandle selected);
    [[nodiscard]] long indexOf(World& world, EntityHandle target) const;

    [[nodiscard]] bool insidePanel(Engine& engine, const glm::vec2& mouse) const;
    [[nodiscard]] int rowAt(Engine& engine, const glm::vec2& mouse) const;

    std::vector<EntityHandle> m_rowEntities;
    size_t m_shownFirst = 0;
    EntityHandle m_dragEntity;
    bool m_dragging = false;
    glm::vec2 m_dragStart{0.0f};
    std::unordered_set<uint32_t> m_expanded;
    size_t m_offset = 0;
    EntityHandle m_lastSelected;
    static constexpr size_t kRows = 16;
};
