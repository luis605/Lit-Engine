module;

#include <GLFW/glfw3.h>
#include <format>
#include <memory>
#include <string>
#include <vector>

export module Editor.inspector;

import Engine.engine;
import Engine.World;
import Engine.History;
import Editor.gizmo;
import Editor.hierarchy;
import Engine.Render.entity;
import Engine.input;
import Engine.glm;

export class Inspector {
  public:
    Inspector();
    ~Inspector();
    void update(Engine& engine, bool pickingEnabled);
    [[nodiscard]] EntityHandle selected() const { return m_selected; }
    void select(EntityHandle e) { m_selected = e; }
    void reset();

  private:
    void handleClick(Engine& engine);
    void handleKeys(Engine& engine);
    void draw(Engine& engine);

    std::unique_ptr<History> m_history;
    Gizmo m_gizmo;
    Hierarchy m_hierarchy;
    EntityHandle m_selected;
    bool m_reparenting = false;
};
