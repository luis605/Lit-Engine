module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

module Engine.History;

import Engine.Render.component;

namespace {
constexpr size_t kMaxAliasHops = 1024;
constexpr int kHandleGenerationShift = 32;

uint64_t aliasKey(EntityHandle handle) { return (static_cast<uint64_t>(handle.generation) << kHandleGenerationShift) | handle.index; }
}

EntityHandle History::resolve(EntityHandle handle) const {
    if (m_aliases.empty()) return handle;
    for (size_t hops = 0; hops < kMaxAliasHops; ++hops) {
        const auto found = m_aliases.find(aliasKey(handle));
        if (found == m_aliases.end()) break;
        handle = found->second;
    }
    return handle;
}

void History::alias(EntityHandle from, EntityHandle to) {
    if (from == to) return;
    m_aliases[aliasKey(from)] = to;
}

struct History::SetLocal : History::Command {
    EntityHandle entity;
    glm::mat4 before;
    glm::mat4 after;
    void apply(History& history) override { history.m_world.setLocalMatrix(history.resolve(entity), after); }
    void revert(History& history) override { history.m_world.setLocalMatrix(history.resolve(entity), before); }
};

struct History::SetVisibleCommand : History::Command {
    EntityHandle entity;
    bool before = true;
    bool after = true;
    void apply(History& history) override { history.m_world.setVisible(history.resolve(entity), after); }
    void revert(History& history) override { history.m_world.setVisible(history.resolve(entity), before); }
};

struct History::SetRenderCommand : History::Command {
    EntityHandle entity;
    RenderableComponent before{};
    RenderableComponent after{};
    static void assign(History& history, EntityHandle target, const RenderableComponent& renderable) {
        history.m_world.setMesh(target, renderable.mesh_uuid);
        history.m_world.setMaterial(target, renderable.material_uuid);
        history.m_world.setShader(target, renderable.shaderId);
        history.m_world.setAlpha(target, renderable.alpha);
    }
    void apply(History& history) override { assign(history, history.resolve(entity), after); }
    void revert(History& history) override { assign(history, history.resolve(entity), before); }
};

struct History::LambdaCommand : History::Command {
    std::function<void()> forward;
    std::function<void()> backward;
    void apply(History&) override {
        if (forward) forward();
    }
    void revert(History&) override {
        if (backward) backward();
    }
};

struct History::Group : History::Command {
    std::vector<std::unique_ptr<Command>> commands;
    void apply(History& history) override {
        for (auto& command : commands) command->apply(history);
    }
    void revert(History& history) override {
        for (size_t index = commands.size(); index-- > 0;) commands[index]->revert(history);
    }
};

struct History::SetName : History::Command {
    EntityHandle entity;
    std::string before;
    std::string after;
    void apply(History& history) override { history.m_world.setName(history.resolve(entity), after); }
    void revert(History& history) override { history.m_world.setName(history.resolve(entity), before); }
};

struct History::SetTag : History::Command {
    EntityHandle entity;
    std::string before;
    std::string after;
    void apply(History& history) override { history.m_world.setTag(history.resolve(entity), after); }
    void revert(History& history) override { history.m_world.setTag(history.resolve(entity), before); }
};

struct History::SetComponent : History::Command {
    EntityHandle entity;
    std::string component;
    std::optional<std::string> before;
    std::string after;
    void apply(History& history) override { history.m_world.setComponentFromText(history.resolve(entity), component, after); }
    void revert(History& history) override {
        const EntityHandle target = history.resolve(entity);
        if (before) {
            history.m_world.setComponentFromText(target, component, *before);
        } else {
            history.m_world.removeComponentByName(target, component);
        }
    }
};

struct History::Reparent : History::Command {
    EntityHandle entity;
    EntityHandle beforeParent;
    EntityHandle afterParent;
    glm::mat4 beforeLocal;
    glm::mat4 afterLocal;
    void place(History& history, EntityHandle parent, const glm::mat4& local) {
        const EntityHandle target = history.resolve(entity);
        history.m_world.setParent(target, history.resolve(parent), false);
        history.m_world.setLocalMatrix(target, local);
    }
    void apply(History& history) override { place(history, afterParent, afterLocal); }
    void revert(History& history) override { place(history, beforeParent, beforeLocal); }
};

struct History::Spawn : History::Command {
    Prefab prefab;
    EntityHandle parent;
    std::vector<EntityHandle> created;
    void apply(History& history) override {
        std::vector<EntityHandle> nodes;
        history.m_world.instantiate(prefab, history.resolve(parent), &nodes);
        const size_t aliased = std::min(nodes.size(), created.size());
        for (size_t index = 0; index < aliased; ++index) history.alias(created[index], nodes[index]);
        created = nodes;
    }
    void revert(History& history) override {
        if (!created.empty()) history.m_world.destroy(history.resolve(created.front()));
    }
};

struct History::Rebuild : History::Command {
    std::function<std::vector<EntityHandle>()> build;
    std::vector<EntityHandle> created;
    void apply(History& history) override {
        const std::vector<EntityHandle> nodes = build();
        const size_t aliased = std::min(nodes.size(), created.size());
        for (size_t index = 0; index < aliased; ++index) history.alias(created[index], nodes[index]);
        created = nodes;
    }
    void revert(History& history) override {
        for (const EntityHandle entity : created) {
            const EntityHandle target = history.resolve(entity);
            if (history.m_world.isAlive(target)) history.m_world.destroy(target);
        }
    }
};

struct History::Remove : History::Command {
    Prefab prefab;
    EntityHandle parent;
    std::vector<EntityHandle> original;
    void apply(History& history) override {
        if (!original.empty()) history.m_world.destroy(history.resolve(original.front()));
    }
    void revert(History& history) override {
        std::vector<EntityHandle> nodes;
        history.m_world.instantiate(prefab, history.resolve(parent), &nodes);
        const size_t aliased = std::min(nodes.size(), original.size());
        for (size_t index = 0; index < aliased; ++index) history.alias(original[index], nodes[index]);
        original = nodes;
    }
};

void History::record(const std::string& label, std::function<void()> apply, std::function<void()> revert) {
    auto command = std::make_unique<LambdaCommand>();
    command->forward = std::move(apply);
    command->backward = std::move(revert);
    push(std::move(command), label);
}

void History::beginGroup(std::string label) {
    if (m_groupDepth++ == 0) m_groupLabel = std::move(label);
}

void History::endGroup() {
    if (m_groupDepth == 0) return;
    if (--m_groupDepth > 0) return;
    if (m_groupCommands.empty()) {
        m_groupLabel.clear();
        return;
    }
    auto group = std::make_unique<Group>();
    std::string label = m_groupLabel.empty() ? m_groupCommands.front()->label : m_groupLabel;
    if (m_groupLabel.empty() && m_groupCommands.size() > 1) label += " x" + std::to_string(m_groupCommands.size());
    group->commands = std::move(m_groupCommands);
    m_groupCommands.clear();
    m_groupLabel.clear();
    push(std::move(group), label);
}

void History::push(std::unique_ptr<Command> command, const std::string& label) {
    command->label = label;
    ++m_revision;
    if (m_groupDepth > 0) {
        m_groupCommands.push_back(std::move(command));
        return;
    }
    m_redo.clear();
    command->serial = m_nextSerial++;
    m_undo.push_back(std::move(command));
    if (m_undo.size() > m_limit) m_undo.erase(m_undo.begin());
}

void History::setLocalMatrix(EntityHandle entity, const glm::mat4& local) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity)) return;
    auto command = std::make_unique<SetLocal>();
    command->entity = entity;
    command->before = m_world.getLocalMatrix(entity);
    command->after = local;
    m_world.setLocalMatrix(entity, local);
    push(std::move(command), "Transform");
}

void History::commitLocalMatrix(EntityHandle entity, const glm::mat4& before, const glm::mat4& after) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity) || std::memcmp(&before, &after, sizeof(glm::mat4)) == 0) return;
    auto command = std::make_unique<SetLocal>();
    command->entity = entity;
    command->before = before;
    command->after = after;
    push(std::move(command), "Transform");
}

void History::setName(EntityHandle entity, const std::string& name) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity) || m_world.getName(entity) == name) return;
    auto command = std::make_unique<SetName>();
    command->entity = entity;
    command->before = m_world.getName(entity);
    command->after = name;
    m_world.setName(entity, name);
    push(std::move(command), "Rename");
}

void History::setTag(EntityHandle entity, const std::string& tag) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity) || m_world.getTag(entity) == tag) return;
    auto command = std::make_unique<SetTag>();
    command->entity = entity;
    command->before = m_world.getTag(entity);
    command->after = tag;
    m_world.setTag(entity, tag);
    push(std::move(command), "Set tag");
}

bool History::setComponentText(EntityHandle entity, const std::string& componentName, const std::string& text) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity)) return false;
    const std::optional<std::string> before = m_world.componentText(entity, componentName);
    if (!m_world.setComponentFromText(entity, componentName, text)) return false;
    auto command = std::make_unique<SetComponent>();
    command->entity = entity;
    command->component = componentName;
    command->before = before;
    command->after = *m_world.componentText(entity, componentName);
    push(std::move(command), "Edit component");
    return true;
}

void History::setVisible(EntityHandle entity, bool visible) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity) || m_world.isVisible(entity) == visible) return;
    auto command = std::make_unique<SetVisibleCommand>();
    command->entity = entity;
    command->before = !visible;
    command->after = visible;
    m_world.setVisible(entity, visible);
    push(std::move(command), "Visibility");
}

void History::setParent(EntityHandle entity, EntityHandle parent, bool keepWorldTransform) {
    entity = resolve(entity);
    parent = resolve(parent);
    if (!m_world.isAlive(entity) || m_world.getParent(entity) == parent) return;
    auto command = std::make_unique<Reparent>();
    command->entity = entity;
    command->beforeParent = m_world.getParent(entity);
    command->beforeLocal = m_world.getLocalMatrix(entity);
    m_world.setParent(entity, parent, keepWorldTransform);
    if (m_world.getParent(entity) != parent) return;
    command->afterParent = parent;
    command->afterLocal = m_world.getLocalMatrix(entity);
    push(std::move(command), "Reparent");
}

EntityHandle History::create(const EntityDesc& desc) {
    const EntityHandle created = m_world.create(desc);
    auto command = std::make_unique<Spawn>();
    command->prefab = m_world.capture(created);
    command->parent = desc.parent;
    command->created = {created};
    push(std::move(command), "Create");
    return created;
}

std::vector<EntityHandle> History::spawnRebuildable(const std::string& label, std::function<std::vector<EntityHandle>()> build) {
    auto command = std::make_unique<Rebuild>();
    command->build = std::move(build);
    command->created = command->build();
    std::vector<EntityHandle> result = command->created;
    push(std::move(command), label);
    return result;
}

EntityHandle History::instantiate(const Prefab& prefab, EntityHandle parent) {
    auto command = std::make_unique<Spawn>();
    command->prefab = prefab;
    command->parent = resolve(parent);
    std::vector<EntityHandle> nodes;
    const EntityHandle root = m_world.instantiate(prefab, command->parent, &nodes);
    command->created = nodes;
    push(std::move(command), "Create");
    return root;
}

void History::destroy(EntityHandle entity) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity)) return;
    auto command = std::make_unique<Remove>();
    command->prefab = m_world.capture(entity);
    command->parent = m_world.getParent(entity);
    command->original.reserve(command->prefab.nodes.size());
    for (const PrefabNode& node : command->prefab.nodes) command->original.push_back({node.sourceIndex, node.sourceGeneration});
    m_world.destroy(entity);
    push(std::move(command), "Delete");
}

void History::setRender(EntityHandle entity, uint32_t mesh, uint32_t material, uint32_t shader, float alpha) {
    entity = resolve(entity);
    if (!m_world.isAlive(entity)) return;
    auto command = std::make_unique<SetRenderCommand>();
    command->entity = entity;
    command->before = m_world.getRenderable(entity);
    command->before.mesh_uuid = m_world.getMesh(entity);
    command->after = command->before;
    command->after.mesh_uuid = mesh;
    command->after.material_uuid = material;
    command->after.shaderId = shader;
    command->after.alpha = alpha;
    if (command->before.mesh_uuid == mesh && command->before.material_uuid == material && command->before.shaderId == shader && command->before.alpha == alpha) return;
    SetRenderCommand::assign(*this, entity, command->after);
    push(std::move(command), "Render");
}

bool History::undo() {
    if (m_undo.empty()) return false;
    ++m_revision;
    auto command = std::move(m_undo.back());
    m_undo.pop_back();
    command->revert(*this);
    m_redo.push_back(std::move(command));
    return true;
}

bool History::redo() {
    if (m_redo.empty()) return false;
    ++m_revision;
    auto command = std::move(m_redo.back());
    m_redo.pop_back();
    command->apply(*this);
    m_undo.push_back(std::move(command));
    return true;
}

void History::clear() {
    m_groupCommands.clear();
    m_groupDepth = 0;
    m_groupLabel.clear();
    ++m_revision;
    m_baseState = m_nextSerial++;
    m_undo.clear();
    m_redo.clear();
    m_aliases.clear();
}
