module;

#include <imgui.h>

#include <algorithm>

module Editor.look;

import Editor.context;
import Engine.engine;
import Engine.renderer;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.glm;

namespace {

constexpr const char* TONEMAPPER_ITEMS = "AgX\0AgX punchy\0ACES\0Khronos PBR Neutral\0Reinhard\0Linear\0";
constexpr int SHADOW_RESOLUTION_BASE = 512;
constexpr int SHADOW_RESOLUTION_STEPS = 3;

int shadowResolutionIndex(int resolution) {
    int index = 0;
    for (int step = 1; step <= SHADOW_RESOLUTION_STEPS; ++step) {
        if (resolution >= (SHADOW_RESOLUTION_BASE << step)) index = step;
    }
    return index;
}

}

void LookPanel::drawColorGrading(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Color grading", ImGuiTreeNodeFlags_DefaultOpen)) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    int tonemapper = static_cast<int>(settings.tonemapper);
    if (ImGui::Combo("Tonemapper", &tonemapper, TONEMAPPER_ITEMS)) settings.tonemapper = static_cast<Tonemapper>(tonemapper);
    ImGui::SliderFloat("Contrast", &settings.contrast, 0.5f, 1.5f);
    ImGui::SliderFloat("Saturation", &settings.saturation, 0.0f, 2.0f);
    ImGui::SliderFloat("White balance", &settings.temperature, 2000.0f, 12000.0f, "%.0f K");
    ImGui::SliderFloat("Tint", &settings.tint, -1.0f, 1.0f);
    ImGui::SliderFloat("Vignette", &settings.vignette, 0.0f, 1.0f);
    ImGui::SliderFloat("Film grain", &settings.grain, 0.0f, 1.0f);
    ImGui::Checkbox("Bloom", &settings.bloom);
    if (settings.bloom) {
        ImGui::SliderFloat("Bloom strength", &settings.bloomIntensity, 0.0f, 0.3f, "%.3f");
        ImGui::SliderFloat("Bloom radius", &settings.bloomRadius, 0.1f, 1.0f);
    }
    ImGui::Checkbox("FXAA", &settings.fxaa);
}

void LookPanel::drawShadows(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Shadows")) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    ImGui::Checkbox("Cascaded shadows", &settings.shadows);
    ImGui::SliderInt("Cascades", &settings.shadowCascades, 1, 4);
    int resolutionIndex = shadowResolutionIndex(settings.shadowResolution);
    if (ImGui::Combo("Resolution", &resolutionIndex, "512\0" "1024\0" "2048\0" "4096\0")) settings.shadowResolution = SHADOW_RESOLUTION_BASE << resolutionIndex;
    ImGui::DragFloat("Distance", &settings.shadowDistance, 1.0f, 10.0f, 2000.0f, "%.0f m");
    ImGui::Checkbox("Contact-hardening (PCSS)", &settings.pcss);
    ImGui::SliderFloat("Softness", &settings.shadowSoftness, 0.1f, 6.0f);
    ImGui::SliderFloat("Normal bias", &settings.shadowNormalBias, 0.0f, 5.0f);
    ImGui::SliderFloat("Depth bias", &settings.shadowDepthBias, 0.0f, 5.0f);
    ImGui::SliderFloat("Cascade blend", &settings.shadowCascadeBlend, 0.0f, 0.5f);
    ImGui::Checkbox("Contact shadows", &settings.contactShadows);
    if (settings.contactShadows) ImGui::SliderFloat("Contact length", &settings.shadowContactLength, 0.05f, 4.0f, "%.2f m");
}

void LookPanel::drawScreenSpace(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Screen space")) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    ImGui::Checkbox("Ground-truth AO", &settings.ssao);
    ImGui::SliderFloat("AO radius", &settings.aoRadius, 0.1f, 5.0f, "%.2f m");
    ImGui::SliderFloat("AO strength", &settings.aoIntensity, 0.2f, 3.0f);
    ImGui::SliderInt("AO slices", &settings.aoSlices, 1, 6);
    ImGui::SliderInt("AO steps", &settings.aoSteps, 2, 12);
    ImGui::SliderInt("AO denoise", &settings.aoDenoise, 0, kAoDenoiseMax);
    ImGui::Checkbox("Bent normals", &settings.bentNormals);
    ImGui::Checkbox("Color bleeding", &settings.colorBleed);
    if (settings.colorBleed) {
        ImGui::SliderFloat("Bleed intensity", &settings.colorBleedIntensity, 0.0f, kColorBleedIntensityMax);
        ImGui::SliderFloat("Bleed radius", &settings.colorBleedRadius, kColorBleedRadiusMin, kColorBleedRadiusMax, "%.2f m");
        ImGui::SliderInt("Bleed quality", &settings.colorBleedQuality, 0, kColorBleedQualityMax);
    }
    int reflectionMode = static_cast<int>(settings.reflectionMode);
    if (ImGui::Combo("Reflections", &reflectionMode, kReflectionModeNames, kReflectionModeCount)) settings.reflectionMode = static_cast<ReflectionMode>(reflectionMode);
    int reflectionQuality = static_cast<int>(settings.reflectionQuality);
    if (ImGui::Combo("Reflection quality", &reflectionQuality, kReflectionQualityNames, kReflectionQualityCount)) settings.reflectionQuality = static_cast<ReflectionQuality>(reflectionQuality);
    ImGui::SliderFloat("Reflection intensity", &settings.reflectionIntensity, 0.0f, 2.0f);
    ImGui::SliderFloat("Reflection trace distance", &settings.reflectionTraceDistance, 5.0f, 300.0f, "%.0f m");
    ImGui::SliderFloat("SSR max roughness", &settings.ssrMaxRoughness, 0.05f, 1.0f);
    ImGui::SliderFloat("SSR thickness", &settings.ssrThickness, 0.05f, 3.0f);
    ImGui::SliderInt("SSR steps", &settings.ssrSteps, 8, 128);
    ImGui::Checkbox("Height fog", &settings.fog);
    if (settings.fog) {
        ImGui::SliderFloat("Fog density", &settings.fogDensity, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Fog falloff", &settings.fogHeightFalloff, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("Fog height", &settings.fogHeight, 0.5f, -1000.0f, 1000.0f, "%.1f m");
    }
    ImGui::Checkbox("Area lights (sphere and tube)", &settings.areaLights);
    ImGui::SliderFloat("Cloud aerial haze", &settings.cloudAerialStrength, 0.0f, 2.0f);
    ImGui::SeparatorText("Upscaling and quality");
    ImGui::SliderFloat("Render scale", &settings.renderScale, kRenderScaleMin, kRenderScaleMax, "%.2f");
    ImGui::SliderFloat("Sharpness", &settings.upscaleSharpness, 0.0f, 1.0f);
    ImGui::Checkbox("Quality governor", &settings.qualityGovernor);
    if (settings.qualityGovernor) {
        ImGui::SliderFloat("Target frame time", &settings.targetFrameMs, kTargetFrameMsMin, 50.0f, "%.1f ms");
        ImGui::SliderFloat("Minimum scale", &settings.governorMinScale, kRenderScaleMin, kRenderScaleMax, "%.2f");
    }
    ImGui::Checkbox("Specular anti-aliasing", &settings.specularAntiAliasing);
    ImGui::Checkbox("Multi-scatter energy compensation", &settings.multiScatter);
}

void LookPanel::drawLevelOfDetail(EditorContext& ctx, LookState& state) {
    if (!ImGui::CollapsingHeader("Level of detail")) return;
    Engine& engine = ctx.engine;
    if (ImGui::SliderFloat("LOD bias", &state.lodBias, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) engine.setLodBias(state.lodBias);
    if (ImGui::SliderInt("Forced LOD", &state.forcedLod, -1, 6, state.forcedLod < 0 ? "automatic" : "LOD %d")) engine.setForcedLod(state.forcedLod);
    if (ImGui::SliderFloat("Cull threshold", &state.smallObjectThreshold, 0.0f, 0.01f, "%.4f")) engine.setSmallObjectThreshold(state.smallObjectThreshold);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projected radius below which objects are skipped. 0 keeps every object.");
}

void LookPanel::drawDebug(EditorContext& ctx, LookState& state) {
    if (!ImGui::CollapsingHeader("Debug")) return;
    Engine& engine = ctx.engine;
    RenderSettings& settings = engine.renderSettings();
    int view = static_cast<int>(settings.debugView);
    if (ImGui::Combo("View", &view, "Lit\0Base color\0Normals\0Roughness\0Metallic\0Ambient occlusion\0Sun shadows\0Reflections\0Lighting only\0Luminance heatmap\0Light clusters\0")) settings.debugView = static_cast<DebugView>(view);
    if (ImGui::Checkbox("Hi-Z depth", &state.debugDepth)) engine.setDebugDepthMode(state.debugDepth);
    ImGui::Checkbox("Animate stress objects", &state.animateStress);
    ImGui::Checkbox("Grid", &ctx.showGrid);
    ImGui::Checkbox("Stats overlay", &ctx.showStats);
    if (ImGui::Button("Reload shaders (F9)")) engine.reloadShaders();
    for (const ShaderError& error : engine.shaderErrors()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s\n%s", error.source.c_str(), error.message.c_str());
}
