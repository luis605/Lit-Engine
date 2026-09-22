module;

#include <algorithm>
#include <vector>

export module Engine.Selection;

import Engine.Render.entity;
import Engine.World;

export class Selection {
  public:
    void clear() { m_items.clear(); }

    void set(EntityHandle e) {
        m_items.clear();
        if (!e.isNull()) m_items.push_back(e);
    }

    void setAll(const std::vector<EntityHandle>& handles) {
        m_items.clear();
        for (EntityHandle h : handles) add(h);
    }

    void add(EntityHandle e) {
        if (e.isNull() || contains(e)) return;
        m_items.push_back(e);
    }

    void remove(EntityHandle e) { std::erase(m_items, e); }

    void toggle(EntityHandle e) {
        if (contains(e)) {
            remove(e);
        } else {
            add(e);
        }
    }

    [[nodiscard]] bool contains(EntityHandle e) const { return std::find(m_items.begin(), m_items.end(), e) != m_items.end(); }
    [[nodiscard]] bool empty() const { return m_items.empty(); }
    [[nodiscard]] size_t size() const { return m_items.size(); }
    [[nodiscard]] EntityHandle primary() const { return m_items.empty() ? NULL_ENTITY : m_items.back(); }
    [[nodiscard]] const std::vector<EntityHandle>& items() const { return m_items; }

    void prune(const World& world) {
        std::erase_if(m_items, [&](EntityHandle e) { return !world.isAlive(e); });
    }

    void removeDescendantsOfSelected(const World& world) {
        std::erase_if(m_items, [&](EntityHandle e) {
            for (EntityHandle p = world.getParent(e); !p.isNull(); p = world.getParent(p)) {
                if (contains(p)) return true;
            }
            return false;
        });
    }

  private:
    std::vector<EntityHandle> m_items;
};
