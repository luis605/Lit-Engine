module;

#include <string>

export module Editor.gizmo;

import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.Render.entity;
import Engine.glm;

export enum class GizmoMode { Translate, Rotate, Scale };

export class Gizmo {
  public:
    bool update(Engine& engine, History& history, EntityHandle selected, bool enabled);
    [[nodiscard]] bool dragging() const { return m_activeAxis >= 0; }
    [[nodiscard]] GizmoMode mode() const { return m_mode; }
    void setMode(GizmoMode mode) { m_mode = mode; }
    void setLocalSpace(bool local) { m_local = local; }
    [[nodiscard]] bool localSpace() const { return m_local; }
    [[nodiscard]] std::string label() const;

  private:
    GizmoMode m_mode = GizmoMode::Translate;
    bool m_local = false;
    int m_activeAxis = -1;
    int m_hoverAxis = -1;
    EntityHandle m_target;
    glm::mat4 m_startLocal{1.0f};
    glm::mat4 m_startWorld{1.0f};
    glm::vec3 m_pivot{0.0f};
    glm::vec3 m_axisDirection{1.0f, 0.0f, 0.0f};
    glm::vec3 m_startVector{1.0f, 0.0f, 0.0f};
    float m_startParameter = 0.0f;
    float m_length = 1.0f;
};
