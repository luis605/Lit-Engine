module;

#include <cstdint>
#include <functional>
#include <istream>
#include <ostream>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

export module Engine.World:Pool;

import Engine.Render.entity;

export class World;

export class IComponentPool {
  public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity entity) = 0;
    virtual bool has(Entity entity) const = 0;
    virtual bool validate(const std::vector<uint8_t>& alive, std::string& reason) const = 0;
    virtual void rename(Entity from, Entity to) = 0;
    virtual void clear() = 0;
};

export template <typename T>
class ComponentPool final : public IComponentPool {
  public:
    template <typename... Args>
    T& emplace(Entity entity, Args&&... args) {
        if (entity >= m_sparse.size()) m_sparse.resize(static_cast<size_t>(entity) + 1, INVALID_ENTITY);
        if (m_sparse[entity] != INVALID_ENTITY) {
            m_data[m_sparse[entity]] = T(std::forward<Args>(args)...);
            return m_data[m_sparse[entity]];
        }
        m_sparse[entity] = static_cast<Entity>(m_data.size());
        m_owners.push_back(entity);
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    [[nodiscard]] const T* find(Entity entity) const {
        if (entity >= m_sparse.size() || m_sparse[entity] == INVALID_ENTITY) return nullptr;
        return &m_data[m_sparse[entity]];
    }

    [[nodiscard]] T* find(Entity entity) {
        if (entity >= m_sparse.size() || m_sparse[entity] == INVALID_ENTITY) return nullptr;
        return &m_data[m_sparse[entity]];
    }

    void remove(Entity entity) override {
        if (entity >= m_sparse.size() || m_sparse[entity] == INVALID_ENTITY) return;
        const Entity slot = m_sparse[entity];
        const Entity last = m_owners.back();
        if (slot != m_data.size() - 1) {
            m_data[slot] = std::move(m_data.back());
            m_owners[slot] = last;
            m_sparse[last] = slot;
        }
        m_sparse[entity] = INVALID_ENTITY;
        m_data.pop_back();
        m_owners.pop_back();
    }

    bool has(Entity entity) const override { return entity < m_sparse.size() && m_sparse[entity] != INVALID_ENTITY; }

    bool validate(const std::vector<uint8_t>& alive, std::string& reason) const override {
        if (m_owners.size() != m_data.size()) {
            reason = "owners and data sizes differ";
            return false;
        }
        for (size_t slot = 0; slot < m_owners.size(); ++slot) {
            const Entity owner = m_owners[slot];
            if (owner >= m_sparse.size() || m_sparse[owner] != slot) {
                reason = "sparse index does not point back to owner slot";
                return false;
            }
            if (owner >= alive.size() || !alive[owner]) {
                reason = "component owned by dead entity " + std::to_string(owner);
                return false;
            }
        }
        size_t mapped = 0;
        for (Entity slot : m_sparse) {
            if (slot != INVALID_ENTITY) ++mapped;
        }
        if (mapped != m_owners.size()) {
            reason = "sparse entries and owners differ in count";
            return false;
        }
        return true;
    }

    void rename(Entity from, Entity to) override {
        if (from >= m_sparse.size() || m_sparse[from] == INVALID_ENTITY) return;
        if (to >= m_sparse.size()) m_sparse.resize(static_cast<size_t>(to) + 1, INVALID_ENTITY);
        const Entity slot = m_sparse[from];
        m_sparse[to] = slot;
        m_owners[slot] = to;
        m_sparse[from] = INVALID_ENTITY;
    }

    void clear() override {
        m_sparse.clear();
        m_owners.clear();
        m_data.clear();
    }

    [[nodiscard]] const std::vector<Entity>& owners() const { return m_owners; }
    [[nodiscard]] std::vector<T>& data() { return m_data; }

  private:
    std::vector<Entity> m_sparse;
    std::vector<Entity> m_owners;
    std::vector<T> m_data;
};

export class Script {
  public:
    virtual ~Script() = default;
    virtual void onStart(World&, EntityHandle) {}
    virtual void onUpdate(World&, EntityHandle, float) {}
    virtual void onFixedUpdate(World&, EntityHandle, float) {}
    virtual void onDestroy(World&, EntityHandle) {}

  private:
    friend class World;
    bool m_started = false;
};

export struct LoadContext {
    std::function<EntityHandle(uint32_t)> resolveFn;
    [[nodiscard]] EntityHandle resolve(uint32_t savedIndex) const { return resolveFn ? resolveFn(savedIndex) : NULL_ENTITY; }
};

export struct ScriptSerializer {
    std::function<bool(const Script&, std::ostream&)> write;
    std::function<bool(World&, EntityHandle, std::istream&, const LoadContext&)> read;
};

export struct ComponentSerializer {
    std::function<void(const IComponentPool&, std::ostream&, Entity)> write;
    std::function<bool(World&, EntityHandle, std::istream&, const LoadContext&)> read;
    std::function<void(const IComponentPool&, const std::function<void(Entity)>&)> owners;
    std::type_index type;
    std::function<bool(World&, EntityHandle)> create;
};
