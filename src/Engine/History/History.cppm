module;

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
    void setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform = true);
    EntityHandle create(const EntityDesc& desc);
    EntityHandle instantiate(const Prefab& prefab, EntityHandle parent = NULL_ENTITY);
    void destroy(EntityHandle e);

    void beginGroup();
    void endGroup();
    [[nodiscard]] bool inGroup() const { return m_groupDepth > 0; }
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
    void clear();

    [[nodiscard]] EntityHandle resolve(EntityHandle h) const;

  private:
    struct Command {
        virtual ~Command() = default;
        virtual void apply(History& history) = 0;
        virtual void revert(History& history) = 0;
    };
    struct SetLocal;
    struct SetVisibleCommand;
    struct Group;
    struct SetName;
    struct SetTag;
    struct SetComponent;
    struct Reparent;
    struct Spawn;
    struct Remove;

    void push(std::unique_ptr<Command> command);
    void alias(EntityHandle from, EntityHandle to);

    World& m_world;
    size_t m_limit;
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    std::vector<std::unique_ptr<Command>> m_groupCommands;
    int m_groupDepth = 0;
    std::unordered_map<uint64_t, EntityHandle> m_aliases;
};
