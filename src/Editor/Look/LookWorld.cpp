module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

module Editor.look;

import Editor.context;
import Engine.engine;
import Engine.renderer;
import Engine.Render.settings;
import Engine.glm;

namespace {

constexpr float CARD_WIDTH = 330.0f;
constexpr float CARD_MIN_HEIGHT = 60.0f;
constexpr float CARD_MIN_MAX_HEIGHT = 200.0f;
constexpr float CARD_RIGHT_MARGIN = 12.0f;
constexpr float CARD_PANEL_GAP = 10.0f;
constexpr float CARD_BOTTOM_MARGIN = 44.0f;
constexpr float CARD_BACKGROUND_ALPHA = 0.92f;

}

void LookPanel::drawWorldSky(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Sky and sun", ImGuiTreeNodeFlags_DefaultOpen)) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    int sky = static_cast<int>(settings.sky);
    if (ImGui::Combo("Sky", &sky, "Physical atmosphere\0Gradient\0HDRI\0")) settings.sky = static_cast<SkyMode>(sky);
    sunDial("worldSun", settings.timeOfDay, settings.sunAzimuth);
    ImGui::SliderFloat("Time of day", &settings.timeOfDay, 0.0f, 24.0f, "%.2f h");
    ImGui::SliderFloat("Sun azimuth", &settings.sunAzimuth, 0.0f, 360.0f, "%.0f°");
    ImGui::DragFloat("Sun", &settings.sunIlluminance, 500.0f, 0.0f, 200000.0f, "%.0f lux");
    ImGui::SliderFloat("Sky brightness", &settings.skyBrightness, 0.0f, 16.0f, "%.2f");
    if (settings.sky == SkyMode::Physical) {
        ImGui::Checkbox("Clouds", &settings.clouds);
        if (settings.clouds) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##coverage", &settings.cloudCoverage, 0.0f, 1.0f, "coverage %.2f");
        }
    }
    if (settings.sky == SkyMode::Gradient) {
        ImGui::ColorEdit3("Zenith", &settings.gradientZenith.x);
        ImGui::ColorEdit3("Horizon", &settings.gradientHorizon.x);
    }
    if (settings.sky == SkyMode::Hdri) ImGui::InputTextWithHint("HDRI", "resources/sky.hdr", &settings.hdriPath);
}

void LookPanel::drawWorldLight(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Light and exposure", ImGuiTreeNodeFlags_DefaultOpen)) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    ImGui::SliderFloat("Ambient (IBL)", &settings.environmentIntensity, 0.0f, 4.0f);
    ImGui::Checkbox("Auto exposure", &settings.autoExposure);
    ImGui::SameLine();
    ImGui::TextDisabled("EV100 %.1f", ctx.engine.exposureEv100());
    if (settings.autoExposure) {
        ImGui::SliderFloat("Compensation", &settings.exposureCompensation, -5.0f, 5.0f, "%+.1f EV");
    } else {
        ImGui::SliderFloat("EV100", &settings.manualEV100, -6.0f, 18.0f, "%.1f");
    }
    int tonemapper = static_cast<int>(settings.tonemapper);
    if (ImGui::Combo("Tonemapper", &tonemapper, "AgX\0AgX punchy\0ACES\0Khronos PBR Neutral\0Reinhard\0Linear\0")) settings.tonemapper = static_cast<Tonemapper>(tonemapper);
}

void LookPanel::drawWorldAtmosphere(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Atmosphere and fog")) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    ImGui::Checkbox("Height fog", &settings.fog);
    if (settings.fog) {
        ImGui::SliderFloat("Density", &settings.fogDensity, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("Height", &settings.fogHeight, 0.5f, -1000.0f, 1000.0f, "%.1f m");
    }
    if (settings.sky == SkyMode::Physical) {
        ImGui::SliderFloat("Haze", &settings.mieScale, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Aerial distance", &settings.aerialDistanceScale, 0.0f, 20.0f);
    }
}

void LookPanel::drawWorldEffects(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Effects")) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    ImGui::Checkbox("Shadows", &settings.shadows);
    ImGui::SameLine(120.0f);
    ImGui::Checkbox("AO", &settings.ssao);
    ImGui::SameLine(200.0f);
    bool reflections = settings.reflectionMode != ReflectionMode::Off;
    if (ImGui::Checkbox("Reflect", &reflections)) settings.reflectionMode = reflections ? ReflectionMode::SSRProbes : ReflectionMode::Off;
    ImGui::Checkbox("Bloom", &settings.bloom);
    ImGui::SameLine(120.0f);
    ImGui::Checkbox("FXAA", &settings.fxaa);
}

void LookPanel::drawWorld(EditorContext& ctx) {
    if (!ctx.selection.empty()) ctx.worldDismissed = false;
    m_worldVisible = !ctx.zen && (ctx.showWorld || (ctx.selection.empty() && ctx.showInspector && !ctx.showLook && !ctx.worldDismissed));
    if (!m_worldVisible) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float right = viewport->WorkPos.x + viewport->WorkSize.x - CARD_RIGHT_MARGIN;
    for (const glm::vec4& panel : ctx.panels) {
        if (panel.x + panel.z > viewport->WorkPos.x + viewport->WorkSize.x * 0.5f) right = std::min(right, panel.x - CARD_PANEL_GAP);
    }
    const float top = ctx.rightColumnTop;
    ImGui::SetNextWindowPos(ImVec2(right - CARD_WIDTH, top), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(CARD_WIDTH, CARD_MIN_HEIGHT), ImVec2(CARD_WIDTH, std::max(CARD_MIN_MAX_HEIGHT, viewport->WorkPos.y + viewport->WorkSize.y - top - CARD_BOTTOM_MARGIN)));
    ImGui::SetNextWindowBgAlpha(CARD_BACKGROUND_ALPHA);
    bool open = true;
    if (!ImGui::Begin("World###WorldCard", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }
    if (!open) {
        ctx.showWorld = false;
        ctx.worldDismissed = true;
    }
    ImGui::TextDisabled("Nothing selected: editing the world. Shift+W toggles.");
    ImGui::PushItemWidth(-110.0f);
    drawWorldSky(ctx);
    drawWorldLight(ctx);
    drawWorldAtmosphere(ctx);
    drawWorldEffects(ctx);
    ImGui::PopItemWidth();
    if (ImGui::SmallButton("All render settings (L)")) ctx.showLook = true;
    ImGui::End();
}
