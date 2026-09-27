module;

#include <cstdint>
#include <vector>

export module Editor.viewport;

import Editor.context;
import Engine.Render.entity;
import Engine.glm;

export class Viewport {
  public:
    void update(EditorContext& ctx, float dt, bool mouseFree);
    void drawOverlays(EditorContext& ctx);
    [[nodiscard]] bool boxSelecting() const { return m_boxing; }
    void cancelInteractions(EditorContext& ctx);

  private:
    void updateFly(EditorContext& ctx, float dt);
    void updateOrbitPan(EditorContext& ctx);
    void updateHover(EditorContext& ctx, float dt);
    void handleClicks(EditorContext& ctx, bool mouseFree);
    void finishBox(EditorContext& ctx);
    void drawGrid(EditorContext& ctx);
    void drawSelection(EditorContext& ctx);
    void drawAxisWidget(EditorContext& ctx);
    void drawIcons(EditorContext& ctx);
    [[nodiscard]] EntityHandle iconAt(const glm::vec2& point) const;
    [[nodiscard]] glm::vec3 pivotFor(EditorContext& ctx) const;

    bool m_pressing = false;
    bool m_boxing = false;
    glm::vec2 m_pressAt{0.0f};
    bool m_orbiting = false;
    bool m_panning = false;
    glm::vec3 m_orbitPivot{0.0f};
    float m_orbitDistance = 10.0f;
    float m_hoverTimer = 0.0f;
    glm::vec2 m_lastHoverMouse{-1.0f};
    double m_lastClickTime = 0.0;
    EntityHandle m_lastClicked;
    struct Icon {
        EntityHandle entity;
        glm::vec2 at{0.0f};
    };
    std::vector<Icon> m_icons;
};
