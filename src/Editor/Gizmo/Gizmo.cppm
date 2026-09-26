module;

#include <string>
#include <vector>

export module Editor.gizmo;

import Editor.context;
import Engine.Render.entity;
import Engine.glm;

export enum class ModalKind { None, Move, Rotate, Scale };

export class TransformTool {
  public:
    void begin(EditorContext& ctx, ModalKind kind);
    void update(EditorContext& ctx);
    void drawHandles(EditorContext& ctx);
    [[nodiscard]] bool active() const { return m_kind != ModalKind::None; }
    [[nodiscard]] bool handleDragging() const { return m_handleDragging; }
    void nudge(EditorContext& ctx, const glm::vec3& screenDirection);
    void resetTransform(EditorContext& ctx, int part);

  private:
    struct Start {
        EntityHandle entity;
        glm::mat4 local{1.0f};
        glm::mat4 world{1.0f};
        glm::mat4 parentWorld{1.0f};
    };

    void capture(EditorContext& ctx);
    void apply(EditorContext& ctx);
    void commit(EditorContext& ctx, const char* label);
    void cancel(EditorContext& ctx);
    [[nodiscard]] glm::vec3 axisDirection(EditorContext& ctx) const;
    [[nodiscard]] std::string status() const;

    ModalKind m_kind = ModalKind::None;
    std::vector<Start> m_starts;
    glm::vec3 m_pivot{0.0f};
    glm::vec2 m_startMouse{0.0f};
    glm::vec2 m_pivotScreen{0.0f};
    glm::vec3 m_startHit{0.0f};
    float m_startParameter = 0.0f;
    int m_axis = -1;
    bool m_plane = false;
    bool m_localAxis = false;
    std::string m_typed;
    float m_value = 0.0f;
    glm::vec3 m_delta{0.0f};
    float m_accumulatedAngle = 0.0f;
    glm::vec2 m_lastAngleVector{1.0f, 0.0f};
    bool m_handleDragging = false;
    std::vector<Start> m_handleStarts;
    glm::mat4 m_handlePrimaryStart{1.0f};
};
