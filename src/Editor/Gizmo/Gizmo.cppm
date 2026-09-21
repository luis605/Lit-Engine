module;

#include <optional>

export module Editor.gizmo;

import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.glm;

export class Gizmo {
  public:
    bool update(Engine& engine, History& history, EntityHandle selected, bool enabled);
    [[nodiscard]] bool dragging() const { return m_activeAxis >= 0; }

  private:
    int m_activeAxis = -1;
    int m_hoverAxis = -1;
    EntityHandle m_target;
    glm::mat4 m_startLocal{1.0f};
    glm::vec3 m_startWorldPosition{0.0f};
    float m_startParameter = 0.0f;
};
