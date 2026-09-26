module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

export module Engine.History;

import Engine.Render.entity;
import Engine.World;
import Engine.glm;

export class History {
  public:
    explicit History(World& world, size_t limit = 256) : m_world(world), m_limit(limit) {}

    void setLocalMatrix(EntityHandle e, const glm::mat4& local);
    void commitLocalMatrix(EntityHandle e, const glm::mat4& before, const glm::mat4& after);
    void setName(EntityHandle e, const std::string& name);
    void setTag(EntityHandle e, const std::string& tag);
    bool setComponentText(EntityHandle e, const std::string& componentName, const std::string& text);
    void setVisible(EntityHandle e, bool visible);
    void setRender(EntityHandle e, uint32_t mesh, uint32_t material, uint32_t shader, float alpha);
    void setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform = true);
    EntityHandle create(const EntityDesc& desc);
    EntityHandle instantiate(const Prefab& prefab, EntityHandle parent = NULL_ENTITY);
    void destroy(EntityHandle e);

    void record(const std::string& label, std::function<void()> apply, std::function<void()> revert);
    void beginGroup(std::string label = {});
    void endGroup();
    [[nodiscard]] bool inGroup() const { return m_groupDepth > 0; }
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
    void clear();
    [[nodiscard]] size_t undoCount() const { return m_undo.size(); }
    [[nodiscard]] size_t redoCount() const { return m_redo.size(); }
    [[nodiscard]] const std::string& undoLabel(size_t index) const { return m_undo[index]->label; }
    [[nodiscard]] const std::string& redoLabel(size_t index) const { return m_redo[index]->label; }
    [[nodiscard]] uint64_t revision() const { return m_revision; }
    [[nodiscard]] uint64_t state() const { return m_undo.empty() ? m_baseState : m_undo.back()->serial; }

    [[nodiscard]] EntityHandle resolve(EntityHandle h) const;

  private:
    struct Command {
        virtual ~Command() = default;
        virtual void apply(History& history) = 0;
        virtual void revert(History& history) = 0;
        std::string label;
        uint64_t serial = 0;
    };
    struct SetLocal;
    struct SetVisibleCommand;
    struct SetRenderCommand;
    struct LambdaCommand;
    struct Group;
    struct SetName;
    struct SetTag;
    struct SetComponent;
    struct Reparent;
    struct Spawn;
    struct Remove;

    void push(std::unique_ptr<Command> command, const char* label);
    void alias(EntityHandle from, EntityHandle to);

    World& m_world;
    size_t m_limit;
    std::vector<std::unique_ptr<Command>> m_undo;
    uint64_t m_nextSerial = 1;
    uint64_t m_baseState = 0;
    std::vector<std::unique_ptr<Command>> m_redo;
    std::vector<std::unique_ptr<Command>> m_groupCommands;
    int m_groupDepth = 0;
    std::string m_groupLabel;
    uint64_t m_revision = 0;
    std::unordered_map<uint64_t, EntityHandle> m_aliases;
};
