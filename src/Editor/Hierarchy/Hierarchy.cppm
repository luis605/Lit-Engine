module;

#include <cstdint>
#include <unordered_set>
#include <vector>

export module Editor.hierarchy;

import Engine.engine;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

export class Hierarchy {
  public:
    void update(Engine& engine, EntityHandle& selected);

  private:
    struct Row {
        EntityHandle entity;
        int depth;
        bool hasChildren;
    };

    void collectRows(World& world, size_t limit, std::vector<Row>& rows) const;
    void reveal(World& world, EntityHandle selected);
    [[nodiscard]] long indexOf(World& world, EntityHandle target) const;

    std::unordered_set<uint32_t> m_expanded;
    size_t m_offset = 0;
    EntityHandle m_lastSelected;
    static constexpr size_t kRows = 16;
};
