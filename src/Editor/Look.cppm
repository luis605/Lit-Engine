module;

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module Editor.look;

import Editor.context;
import Engine.Render.settings;

export struct LookState {
    float lodBias = 1.0f;
    int forcedLod = -1;
    float smallObjectThreshold = 0.0f;
    bool debugDepth = false;
    bool animateStress = true;
};

export bool applyQualityPreset(RenderSettings& settings, std::string_view name);
export std::string activeQualityPreset(const RenderSettings& settings);

export class LookPanel {
  public:
    void draw(EditorContext& ctx, LookState& state);
    void drawWorld(EditorContext& ctx);
    [[nodiscard]] bool worldVisible() const { return m_worldVisible; }

  private:
    static bool sunDial(const char* id, float& hours, float& azimuth);
    void drawQuality(EditorContext& ctx);
    void drawSkyAndSun(EditorContext& ctx);
    void drawCameraAndExposure(EditorContext& ctx);
    void drawColorGrading(EditorContext& ctx);
    void drawShadows(EditorContext& ctx);
    void drawScreenSpace(EditorContext& ctx);
    void drawLevelOfDetail(EditorContext& ctx, LookState& state);
    void drawDebug(EditorContext& ctx, LookState& state);
    void drawGpu(EditorContext& ctx, double gpuMilliseconds);
    void drawWorldSky(EditorContext& ctx);
    void drawWorldLight(EditorContext& ctx);
    void drawWorldAtmosphere(EditorContext& ctx);
    void drawWorldEffects(EditorContext& ctx);

    std::vector<float> m_gpuHistory = std::vector<float>(120, 0.0f);
    size_t m_historyHead = 0;
    bool m_worldVisible = false;
};
