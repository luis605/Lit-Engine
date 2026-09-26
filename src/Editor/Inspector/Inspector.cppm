module;

#include <cstdint>
#include <string>
#include <vector>

export module Editor.inspector;

import Editor.context;
import Engine.Render.entity;
import Engine.glm;

export class Inspector {
  public:
    void draw(EditorContext& ctx);
    void focusName() { m_focusName = true; }

  private:
    struct Snapshot {
        EntityHandle entity;
        glm::mat4 local{1.0f};
    };

    void drawHeader(EditorContext& ctx, EntityHandle e);
    void drawTransform(EditorContext& ctx, EntityHandle e);
    void drawRender(EditorContext& ctx, EntityHandle e);
    void drawMaterial(EditorContext& ctx, uint32_t id);
    void drawComponents(EditorContext& ctx, EntityHandle e);
    void beginTransformEdit(EditorContext& ctx);
    void endTransformEdit(EditorContext& ctx, const char* label);

    bool m_focusName = false;
    std::string m_nameBuffer;
    std::string m_tagBuffer;
    EntityHandle m_bufferEntity;
    std::vector<Snapshot> m_editStart;
    bool m_editing = false;
    glm::vec2 m_cardPos{-1.0f};
    glm::vec2 m_setPos{-1.0f};
    float m_lastHeight = 300.0f;
    std::string m_componentBuffer;
    std::string m_componentEditing;
    std::string m_materialFilter;
    bool m_materialDirty = false;
};
