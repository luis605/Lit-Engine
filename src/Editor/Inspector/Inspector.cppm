module;

#include <optional>

#include <cstdint>
#include <string>
#include <vector>

export module Editor.inspector;

import Editor.context;
import Engine.Render.entity;
import Engine.World;
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

    void drawHeader(EditorContext& ctx, EntityHandle entity);
    void drawNameRow(EditorContext& ctx, EntityHandle entity);
    void drawParentRow(EditorContext& ctx, EntityHandle entity);
    void drawTagAndLayers(EditorContext& ctx, EntityHandle entity);
    void drawLayerButtons(EditorContext& ctx, EntityHandle entity);
    void drawTransform(EditorContext& ctx, EntityHandle entity);
    void drawRender(EditorContext& ctx, EntityHandle entity);
    void drawMeshCombo(EditorContext& ctx, EntityHandle entity);
    void drawMaterialCombo(EditorContext& ctx, uint32_t material);
    void drawShaderCombo(EditorContext& ctx, uint32_t shaderId);
    void drawAlphaSlider(EditorContext& ctx, float currentAlpha);
    void drawMaterial(EditorContext& ctx, uint32_t id);
    void drawComponents(EditorContext& ctx, EntityHandle entity, bool expand);
    void drawLightComponent(EditorContext& ctx, EntityHandle entity);
    void drawCameraComponent(EditorContext& ctx, EntityHandle entity);
    void drawProbeComponent(EditorContext& ctx, EntityHandle entity);
    void drawPlanarComponent(EditorContext& ctx, EntityHandle entity);
    void drawIkChainComponent(EditorContext& ctx, EntityHandle entity);
    void drawWalkerComponent(EditorContext& ctx, EntityHandle entity);
    void drawIntensityPresets(EditorContext& ctx, LightComponent& light, bool& committed);
    bool drawEntityPicker(EditorContext& ctx, const char* label, EntityHandle& value);
    void captureComponentBefore(EditorContext& ctx, EntityHandle entity, const std::string& name);
    void commitComponentEdit(EditorContext& ctx, EntityHandle entity, const std::string& name);
    void drawTextComponent(EditorContext& ctx, EntityHandle entity, const std::string& name, const std::string& text);
    void drawAddComponent(EditorContext& ctx, EntityHandle entity, const std::vector<std::pair<std::string, std::string>>& existing);
    void assignRender(EditorContext& ctx, const char* label, bool changeMesh, uint32_t mesh, bool changeMaterial, uint32_t material, bool changeShader, uint32_t shader, bool changeAlpha, float alpha);
    glm::vec2 placeCard(EditorContext& ctx, EntityHandle entity, float width, float height);
    void positionWindow(EditorContext& ctx, EntityHandle entity, float width, float maxHeight);
    void drawLeaderLine();
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
    std::string m_componentBefore;
    std::string m_pickerFilter;
    std::string m_componentEditing;
    std::string m_materialFilter;
    bool m_dock = false;
    int m_side = 1;
    glm::vec2 m_lastTarget{-1.0f};
    std::optional<glm::vec2> m_leader;
    std::string m_sections;
};
