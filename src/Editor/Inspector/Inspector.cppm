module;

#include <optional>

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
    void dock() { m_dock = true; }
    [[nodiscard]] const std::string& sections() const { return m_sections; }

  private:
    struct Snapshot {
        EntityHandle entity;
        glm::mat4 local{1.0f};
    };

    void drawHeader(EditorContext& ctx, EntityHandle e);
    void drawTransform(EditorContext& ctx, EntityHandle e);
    void drawRender(EditorContext& ctx, EntityHandle e);
    void drawMaterial(EditorContext& ctx, uint32_t id);
    void drawComponents(EditorContext& ctx, EntityHandle e, bool expand);
    glm::vec2 placeCard(EditorContext& ctx, EntityHandle e, float width, float height);
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
    bool m_dock = false;
    int m_side = 1;
    glm::vec2 m_lastTarget{-1.0f};
    std::optional<glm::vec2> m_leader;
    std::string m_sections;
};
