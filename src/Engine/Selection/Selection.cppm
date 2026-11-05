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

    void set(EntityHandle entity) {
        clear();
        add(entity);
    }

    void setAll(const std::vector<EntityHandle>& handles) {
        clear();
        m_members.reserve(handles.size());
        m_items.reserve(handles.size());
        for (const EntityHandle handle : handles) add(handle);
    }

    void add(EntityHandle entity) {
        if (entity.isNull() || !m_members.insert(key(entity)).second) return;
        m_items.push_back(entity);
    }

    void remove(EntityHandle entity) {
        if (m_members.erase(key(entity)) > 0) std::erase(m_items, entity);
    }

    void toggle(EntityHandle entity) {
        if (contains(entity)) {
            remove(entity);
        } else {
            add(entity);
        }
    }

    [[nodiscard]] bool contains(EntityHandle entity) const { return m_members.contains(key(entity)); }
    [[nodiscard]] bool empty() const { return m_items.empty(); }
    [[nodiscard]] size_t size() const { return m_items.size(); }
    [[nodiscard]] EntityHandle primary() const { return m_items.empty() ? NULL_ENTITY : m_items.back(); }
    [[nodiscard]] const std::vector<EntityHandle>& items() const { return m_items; }

    void prune(const World& world) {
        const size_t before = m_items.size();
        std::erase_if(m_items, [&](EntityHandle entity) { return !world.isAlive(entity); });
        if (m_items.size() != before) rebuild();
    }

    void removeDescendantsOfSelected(const World& world) {
        const size_t before = m_items.size();
        std::erase_if(m_items, [&](EntityHandle entity) {
            for (EntityHandle ancestor = world.getParent(entity); !ancestor.isNull(); ancestor = world.getParent(ancestor)) {
                if (contains(ancestor)) return true;
            }
            return false;
        });
        if (m_items.size() != before) rebuild();
    }

  private:
    static uint64_t key(EntityHandle entity) { return (static_cast<uint64_t>(entity.generation) << 32) | entity.index; }

    void rebuild() {
        m_members.clear();
        for (const EntityHandle entity : m_items) m_members.insert(key(entity));
    }

    std::vector<EntityHandle> m_items;
    std::unordered_set<uint64_t> m_members;
};
