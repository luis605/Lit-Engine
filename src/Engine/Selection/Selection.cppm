module;

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

export module Engine.Selection;

import Engine.Render.entity;
import Engine.World;

export class Selection {
  public:
    void clear() {
        m_items.clear();
        m_members.clear();
    }

    void set(EntityHandle e) {
        clear();
        add(e);
    }

    void setAll(const std::vector<EntityHandle>& handles) {
        clear();
        m_members.reserve(handles.size());
        m_items.reserve(handles.size());
        for (EntityHandle h : handles) add(h);
    }

    void add(EntityHandle e) {
        if (e.isNull() || !m_members.insert(key(e)).second) return;
        m_items.push_back(e);
    }

    void remove(EntityHandle e) {
        if (m_members.erase(key(e)) > 0) std::erase(m_items, e);
    }

    void toggle(EntityHandle e) {
        if (contains(e)) {
            remove(e);
        } else {
            add(e);
        }
    }

    [[nodiscard]] bool contains(EntityHandle e) const { return m_members.contains(key(e)); }
    [[nodiscard]] bool empty() const { return m_items.empty(); }
    [[nodiscard]] size_t size() const { return m_items.size(); }
    [[nodiscard]] EntityHandle primary() const { return m_items.empty() ? NULL_ENTITY : m_items.back(); }
    [[nodiscard]] const std::vector<EntityHandle>& items() const { return m_items; }

    void prune(const World& world) {
        const size_t before = m_items.size();
        std::erase_if(m_items, [&](EntityHandle e) { return !world.isAlive(e); });
        if (m_items.size() != before) rebuild();
    }

    void removeDescendantsOfSelected(const World& world) {
        const size_t before = m_items.size();
        std::erase_if(m_items, [&](EntityHandle e) {
            for (EntityHandle p = world.getParent(e); !p.isNull(); p = world.getParent(p)) {
                if (contains(p)) return true;
            }
            return false;
        });
        if (m_items.size() != before) rebuild();
    }

  private:
    static uint64_t key(EntityHandle e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; }

    void rebuild() {
        m_members.clear();
        for (EntityHandle e : m_items) m_members.insert(key(e));
    }

    std::vector<EntityHandle> m_items;
    std::unordered_set<uint64_t> m_members;
};
