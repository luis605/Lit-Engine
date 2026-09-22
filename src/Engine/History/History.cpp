module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

module Engine.History;

namespace {
uint64_t key(EntityHandle h) { return (static_cast<uint64_t>(h.generation) << 32) | h.index; }
}

EntityHandle History::resolve(EntityHandle h) const {
    for (size_t hops = 0; hops < 1024; ++hops) {
        const auto it = m_aliases.find(key(h));
        if (it == m_aliases.end()) break;
        h = it->second;
    }
    return h;
}

void History::alias(EntityHandle from, EntityHandle to) {
    if (from == to) return;
    m_aliases[key(from)] = to;
}

struct History::SetLocal : History::Command {
    EntityHandle entity;
    glm::mat4 before;
    glm::mat4 after;
    void apply(History& h) override { h.m_world.setLocalMatrix(h.resolve(entity), after); }
    void revert(History& h) override { h.m_world.setLocalMatrix(h.resolve(entity), before); }
};

struct History::SetVisibleCommand : History::Command {
    EntityHandle entity;
    bool before = true;
    bool after = true;
    void apply(History& h) override { h.m_world.setVisible(h.resolve(entity), after); }
    void revert(History& h) override { h.m_world.setVisible(h.resolve(entity), before); }
};

struct History::SetName : History::Command {
    EntityHandle entity;
    std::string before;
    std::string after;
    void apply(History& h) override { h.m_world.setName(h.resolve(entity), after); }
    void revert(History& h) override { h.m_world.setName(h.resolve(entity), before); }
};

struct History::SetTag : History::Command {
    EntityHandle entity;
    std::string before;
    std::string after;
    void apply(History& h) override { h.m_world.setTag(h.resolve(entity), after); }
    void revert(History& h) override { h.m_world.setTag(h.resolve(entity), before); }
};

struct History::SetComponent : History::Command {
    EntityHandle entity;
    std::string component;
    std::optional<std::string> before;
    std::string after;
    void apply(History& h) override { h.m_world.setComponentFromText(h.resolve(entity), component, after); }
    void revert(History& h) override {
        const EntityHandle e = h.resolve(entity);
        if (before) {
            h.m_world.setComponentFromText(e, component, *before);
        } else {
            h.m_world.removeComponentByName(e, component);
        }
    }
};

struct History::Reparent : History::Command {
    EntityHandle entity;
    EntityHandle beforeParent;
    EntityHandle afterParent;
    glm::mat4 beforeLocal;
    glm::mat4 afterLocal;
    void place(History& h, EntityHandle parent, const glm::mat4& local) {
        const EntityHandle e = h.resolve(entity);
        h.m_world.setParent(e, h.resolve(parent), false);
        h.m_world.setLocalMatrix(e, local);
    }
    void apply(History& h) override { place(h, afterParent, afterLocal); }
    void revert(History& h) override { place(h, beforeParent, beforeLocal); }
};

struct History::Spawn : History::Command {
    Prefab prefab;
    EntityHandle parent;
    std::vector<EntityHandle> created;
    void apply(History& h) override {
        std::vector<EntityHandle> nodes;
        h.m_world.instantiate(prefab, h.resolve(parent), &nodes);
        for (size_t i = 0; i < nodes.size() && i < created.size(); ++i) h.alias(created[i], nodes[i]);
        created = nodes;
    }
    void revert(History& h) override {
        if (!created.empty()) h.m_world.destroy(h.resolve(created.front()));
    }
};

struct History::Remove : History::Command {
    Prefab prefab;
    EntityHandle parent;
    std::vector<EntityHandle> original;
    void apply(History& h) override {
        if (!original.empty()) h.m_world.destroy(h.resolve(original.front()));
    }
    void revert(History& h) override {
        std::vector<EntityHandle> nodes;
        h.m_world.instantiate(prefab, h.resolve(parent), &nodes);
        for (size_t i = 0; i < nodes.size() && i < original.size(); ++i) h.alias(original[i], nodes[i]);
        original = nodes;
    }
};

void History::push(std::unique_ptr<Command> command) {
    m_redo.clear();
    m_undo.push_back(std::move(command));
    if (m_undo.size() > m_limit) m_undo.erase(m_undo.begin());
}

void History::setLocalMatrix(EntityHandle e, const glm::mat4& local) {
    e = resolve(e);
    if (!m_world.isAlive(e)) return;
    auto command = std::make_unique<SetLocal>();
    command->entity = e;
    command->before = m_world.getLocalMatrix(e);
    command->after = local;
    m_world.setLocalMatrix(e, local);
    push(std::move(command));
}

void History::commitLocalMatrix(EntityHandle e, const glm::mat4& before, const glm::mat4& after) {
    e = resolve(e);
    if (!m_world.isAlive(e) || std::memcmp(&before, &after, sizeof(glm::mat4)) == 0) return;
    auto command = std::make_unique<SetLocal>();
    command->entity = e;
    command->before = before;
    command->after = after;
    push(std::move(command));
}

void History::setName(EntityHandle e, const std::string& name) {
    e = resolve(e);
    if (!m_world.isAlive(e) || m_world.getName(e) == name) return;
    auto command = std::make_unique<SetName>();
    command->entity = e;
    command->before = m_world.getName(e);
    command->after = name;
    m_world.setName(e, name);
    push(std::move(command));
}

void History::setTag(EntityHandle e, const std::string& tag) {
    e = resolve(e);
    if (!m_world.isAlive(e) || m_world.getTag(e) == tag) return;
    auto command = std::make_unique<SetTag>();
    command->entity = e;
    command->before = m_world.getTag(e);
    command->after = tag;
    m_world.setTag(e, tag);
    push(std::move(command));
}

bool History::setComponentText(EntityHandle e, const std::string& componentName, const std::string& text) {
    e = resolve(e);
    if (!m_world.isAlive(e)) return false;
    const std::optional<std::string> before = m_world.componentText(e, componentName);
    if (!m_world.setComponentFromText(e, componentName, text)) return false;
    auto command = std::make_unique<SetComponent>();
    command->entity = e;
    command->component = componentName;
    command->before = before;
    command->after = *m_world.componentText(e, componentName);
    push(std::move(command));
    return true;
}

void History::setVisible(EntityHandle e, bool visible) {
    e = resolve(e);
    if (!m_world.isAlive(e) || m_world.isVisible(e) == visible) return;
    auto command = std::make_unique<SetVisibleCommand>();
    command->entity = e;
    command->before = !visible;
    command->after = visible;
    m_world.setVisible(e, visible);
    push(std::move(command));
}

void History::setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform) {
    e = resolve(e);
    parent = resolve(parent);
    if (!m_world.isAlive(e) || m_world.getParent(e) == parent) return;
    auto command = std::make_unique<Reparent>();
    command->entity = e;
    command->beforeParent = m_world.getParent(e);
    command->beforeLocal = m_world.getLocalMatrix(e);
    m_world.setParent(e, parent, keepWorldTransform);
    if (m_world.getParent(e) != parent) return;
    command->afterParent = parent;
    command->afterLocal = m_world.getLocalMatrix(e);
    push(std::move(command));
}

EntityHandle History::create(const EntityDesc& desc) {
    const EntityHandle e = m_world.create(desc);
    auto command = std::make_unique<Spawn>();
    command->prefab = m_world.capture(e);
    command->parent = desc.parent;
    command->created = {e};
    push(std::move(command));
    return e;
}

EntityHandle History::instantiate(const Prefab& prefab, EntityHandle parent) {
    auto command = std::make_unique<Spawn>();
    command->prefab = prefab;
    command->parent = resolve(parent);
    std::vector<EntityHandle> nodes;
    const EntityHandle root = m_world.instantiate(prefab, command->parent, &nodes);
    command->created = nodes;
    push(std::move(command));
    return root;
}

void History::destroy(EntityHandle e) {
    e = resolve(e);
    if (!m_world.isAlive(e)) return;
    auto command = std::make_unique<Remove>();
    command->prefab = m_world.capture(e);
    command->parent = m_world.getParent(e);
    for (const PrefabNode& node : command->prefab.nodes) command->original.push_back({node.sourceIndex, node.sourceGeneration});
    m_world.destroy(e);
    push(std::move(command));
}

bool History::undo() {
    if (m_undo.empty()) return false;
    auto command = std::move(m_undo.back());
    m_undo.pop_back();
    command->revert(*this);
    m_redo.push_back(std::move(command));
    return true;
}

bool History::redo() {
    if (m_redo.empty()) return false;
    auto command = std::move(m_redo.back());
    m_redo.pop_back();
    command->apply(*this);
    m_undo.push_back(std::move(command));
    return true;
}

void History::clear() {
    m_undo.clear();
    m_redo.clear();
    m_aliases.clear();
}
