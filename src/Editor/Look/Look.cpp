module;

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

module Editor.look;

import Editor.context;
import Engine.engine;
import Engine.renderer;
import Engine.World;
import Engine.camera;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.glm;

namespace {

bool sunDial(const char* id, float& hours, float& azimuth) {
    ImGui::PushID(id);
    const float size = std::min(ImGui::GetContentRegionAvail().x, 150.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("dial", ImVec2(size, size * 0.62f));
    const bool active = ImGui::IsItemActive();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.55f);
    const float radius = size * 0.45f;
    draw->AddRectFilledMultiColor(ImVec2(origin.x, origin.y), ImVec2(origin.x + size, center.y), IM_COL32(40, 70, 130, 255), IM_COL32(40, 70, 130, 255), IM_COL32(230, 150, 80, 255), IM_COL32(230, 150, 80, 255));
    draw->AddRectFilled(ImVec2(origin.x, center.y), ImVec2(origin.x + size, origin.y + size * 0.62f), IM_COL32(35, 32, 30, 255));
    draw->PathArcTo(center, radius, 3.14159f, 6.28318f, 32);
    draw->PathStroke(IM_COL32(255, 255, 255, 60), 0, 1.5f);
    const float t = std::clamp((hours - 6.0f) / 12.0f, -0.25f, 1.25f);
    const float angle = 3.14159f + t * 3.14159f;
    const ImVec2 sun(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
    const bool night = hours < 6.0f || hours > 18.0f;
    draw->AddCircleFilled(sun, 7.0f, night ? IM_COL32(180, 190, 220, 255) : IM_COL32(255, 220, 120, 255), 16);
    draw->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), IM_COL32(255, 255, 255, 220), std::format("{:02d}:{:02d}", static_cast<int>(hours) % 24, static_cast<int>(std::fmod(hours, 1.0f) * 60.0f)).c_str());
    bool changed = false;
    if (active) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float a = std::atan2(mouse.y - center.y, mouse.x - center.x);
        float tt = (a < 0.0f ? a + 6.28318f : a) / 3.14159f - 1.0f;
        if (mouse.y > center.y) tt = mouse.x < center.x ? -0.1f : 1.1f;
        hours = std::clamp(6.0f + tt * 12.0f, 0.0f, 24.0f);
        if (ImGui::GetIO().KeyShift) azimuth = std::fmod(azimuth + ImGui::GetIO().MouseDelta.x + 360.0f, 360.0f);
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the sun across the sky. Shift+drag spins its azimuth.");
    ImGui::PopID();
    return changed;
}

}  // namespace

void LookPanel::draw(EditorContext& ctx, LookState& state) {
    const double gpu = ctx.engine.gpuTotalMilliseconds();
    m_gpuHistory[m_historyHead] = static_cast<float>(gpu);
    m_historyHead = (m_historyHead + 1) % m_gpuHistory.size();
    if (!ctx.showLook || ctx.zen) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float top = ctx.rightColumnTop;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 10.0f, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, std::max(240.0f, viewport->WorkPos.y + viewport->WorkSize.y - top - 44.0f)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);
    if (!ImGui::Begin("Look###LookPanel", &ctx.showLook, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }
    ctx.panels.emplace_back(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    Engine& engine = ctx.engine;
    RenderSettings& r = engine.renderSettings();
    Camera& camera = ctx.world.camera();
    ImGui::PushItemWidth(-120.0f);

    if (ImGui::CollapsingHeader("Sky and sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        int sky = static_cast<int>(r.sky);
        if (ImGui::Combo("Sky", &sky, "Physical atmosphere\0Gradient\0HDRI\0")) r.sky = static_cast<SkyMode>(sky);
        sunDial("sun", r.timeOfDay, r.sunAzimuth);
        ImGui::SliderFloat("Time of day", &r.timeOfDay, 0.0f, 24.0f, "%.2f h");
        ImGui::SliderFloat("Sun azimuth", &r.sunAzimuth, 0.0f, 360.0f, "%.0f°");
        ImGui::Checkbox("Animate time", &r.animateTime);
        if (r.animateTime) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##speed", &r.timeSpeed, 0.01f, 4.0f, "%.2f h/s", ImGuiSliderFlags_Logarithmic);
        }
        ImGui::Checkbox("Sun follows a Sun light entity", &r.sunFollowsLight);
        ImGui::DragFloat("Sun illuminance", &r.sunIlluminance, 500.0f, 0.0f, 200000.0f, "%.0f lux");
        ImGui::SliderFloat("Sun size", &r.sunAngularDiameter, 0.1f, 5.0f, "%.2f°");
        if (r.sky == SkyMode::Physical && ImGui::TreeNode("Atmosphere")) {
            ImGui::SliderFloat("Rayleigh", &r.rayleighScale, 0.0f, 4.0f);
            ImGui::SliderFloat("Mie (haze)", &r.mieScale, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Mie anisotropy", &r.mieAnisotropy, 0.0f, 0.95f);
            ImGui::SliderFloat("Ozone", &r.ozoneScale, 0.0f, 3.0f);
            ImGui::SliderFloat("Ground albedo", &r.groundAlbedo, 0.0f, 1.0f);
            ImGui::SliderFloat("Aerial distance", &r.aerialDistanceScale, 0.0f, 20.0f);
            ImGui::TreePop();
        }
        if (r.sky == SkyMode::Physical && ImGui::TreeNode("Clouds and stars")) {
            ImGui::Checkbox("Clouds", &r.clouds);
            ImGui::SliderFloat("Coverage", &r.cloudCoverage, 0.0f, 1.0f);
            ImGui::SliderFloat("Density", &r.cloudDensity, 0.0f, 3.0f);
            ImGui::SliderFloat("Wind", &r.cloudSpeed, 0.0f, 80.0f, "%.1f m/s");
            ImGui::DragFloat("Altitude", &r.cloudAltitude, 10.0f, 200.0f, 12000.0f, "%.0f m");
            ImGui::Checkbox("Stars at night", &r.stars);
            ImGui::TreePop();
        }
        if (r.sky == SkyMode::Gradient) {
            ImGui::ColorEdit3("Zenith", &r.gradientZenith.x);
            ImGui::ColorEdit3("Horizon", &r.gradientHorizon.x);
            ImGui::ColorEdit3("Ground", &r.gradientGround.x);
            ImGui::DragFloat("Brightness", &r.gradientIntensity, 50.0f, 0.0f, 100000.0f, "%.0f nits");
        }
        if (r.sky == SkyMode::Hdri) {
            ImGui::InputTextWithHint("HDRI file", "resources/sky.hdr", &r.hdriPath);
            ImGui::DragFloat("Brightness", &r.hdriIntensity, 50.0f, 0.0f, 200000.0f, "%.0f nits");
            ImGui::SliderFloat("Rotation", &r.hdriRotation, 0.0f, 360.0f, "%.0f°");
        }
        ImGui::SliderFloat("Environment light", &r.environmentIntensity, 0.0f, 4.0f);
        ImGui::SliderFloat("Sky brightness", &r.skyBrightness, 0.0f, 4.0f);
    }
    if (ImGui::CollapsingHeader("Camera and exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Auto exposure", &r.autoExposure);
        ImGui::SameLine();
        ImGui::TextDisabled("EV100 %.1f", engine.exposureEv100());
        if (r.autoExposure) {
            ImGui::SliderFloat("Compensation", &r.exposureCompensation, -5.0f, 5.0f, "%+.1f EV");
            ImGui::SliderFloat("Adapt brighter", &r.adaptationUp, 0.1f, 10.0f);
            ImGui::SliderFloat("Adapt darker", &r.adaptationDown, 0.1f, 10.0f);
            ImGui::DragFloatRange2("EV range", &r.minEV100, &r.maxEV100, 0.1f, -10.0f, 20.0f, "%.1f", "%.1f");
        } else {
            ImGui::SliderFloat("EV100", &r.manualEV100, -6.0f, 18.0f, "%.1f");
            if (ImGui::Button("Sunny 16")) r.manualEV100 = 15.0f;
            ImGui::SameLine();
            if (ImGui::Button("Overcast")) r.manualEV100 = 12.0f;
            ImGui::SameLine();
            if (ImGui::Button("Indoor")) r.manualEV100 = 7.0f;
            ImGui::SameLine();
            if (ImGui::Button("Night")) r.manualEV100 = 0.0f;
        }
        float fov = camera.getFov();
        if (ImGui::SliderFloat("Field of view", &fov, 10.0f, 120.0f, "%.0f°")) camera.setFov(fov);
        float nearPlane = camera.getNearPlane();
        float farPlane = camera.getFarPlane();
        if (ImGui::DragFloat("Near", &nearPlane, 0.005f, 0.001f, 10.0f, "%.3f")) camera.setNearPlane(std::max(nearPlane, 0.001f));
        if (ImGui::DragFloat("Far", &farPlane, 5.0f, 10.0f, 1000000.0f, "%.0f")) camera.setFarPlane(std::max(farPlane, nearPlane + 1.0f));
        ImGui::SliderFloat("Fly speed", &ctx.flySpeed, 0.1f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
    }
    if (ImGui::CollapsingHeader("Color grading", ImGuiTreeNodeFlags_DefaultOpen)) {
        int op = static_cast<int>(r.tonemapper);
        if (ImGui::Combo("Tonemapper", &op, "AgX\0AgX punchy\0ACES\0Khronos PBR Neutral\0Reinhard\0Linear\0")) r.tonemapper = static_cast<Tonemapper>(op);
        ImGui::SliderFloat("Contrast", &r.contrast, 0.5f, 1.5f);
        ImGui::SliderFloat("Saturation", &r.saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("White balance", &r.temperature, 2000.0f, 12000.0f, "%.0f K");
        ImGui::SliderFloat("Tint", &r.tint, -1.0f, 1.0f);
        ImGui::SliderFloat("Vignette", &r.vignette, 0.0f, 1.0f);
        ImGui::SliderFloat("Film grain", &r.grain, 0.0f, 1.0f);
        ImGui::Checkbox("Bloom", &r.bloom);
        if (r.bloom) {
            ImGui::SliderFloat("Bloom strength", &r.bloomIntensity, 0.0f, 0.3f, "%.3f");
            ImGui::SliderFloat("Bloom radius", &r.bloomRadius, 0.1f, 1.0f);
        }
        ImGui::Checkbox("FXAA", &r.fxaa);
    }
    if (ImGui::CollapsingHeader("Shadows")) {
        ImGui::Checkbox("Cascaded shadows", &r.shadows);
        ImGui::SliderInt("Cascades", &r.shadowCascades, 1, 4);
        int resolutionIndex = r.shadowResolution >= 4096 ? 3 : r.shadowResolution >= 2048 ? 2 : r.shadowResolution >= 1024 ? 1 : 0;
        if (ImGui::Combo("Resolution", &resolutionIndex, "512\0" "1024\0" "2048\0" "4096\0")) r.shadowResolution = 512 << resolutionIndex;
        ImGui::DragFloat("Distance", &r.shadowDistance, 1.0f, 10.0f, 2000.0f, "%.0f m");
        ImGui::Checkbox("Contact-hardening (PCSS)", &r.pcss);
        ImGui::SliderFloat("Softness", &r.shadowSoftness, 0.1f, 6.0f);
        ImGui::SliderFloat("Normal bias", &r.shadowNormalBias, 0.0f, 5.0f);
        ImGui::SliderFloat("Depth bias", &r.shadowDepthBias, 0.0f, 5.0f);
        ImGui::Checkbox("Stagger far cascades", &r.staggerCascades);
    }
    if (ImGui::CollapsingHeader("Screen space")) {
        ImGui::Checkbox("Ground-truth AO", &r.ssao);
        ImGui::SliderFloat("AO radius", &r.aoRadius, 0.1f, 5.0f, "%.2f m");
        ImGui::SliderFloat("AO strength", &r.aoIntensity, 0.2f, 3.0f);
        ImGui::SliderInt("AO slices", &r.aoSlices, 1, 6);
        ImGui::SliderInt("AO steps", &r.aoSteps, 2, 12);
        ImGui::Checkbox("Reflections (SSR)", &r.ssr);
        ImGui::SliderFloat("SSR max roughness", &r.ssrMaxRoughness, 0.05f, 1.0f);
        ImGui::SliderFloat("SSR thickness", &r.ssrThickness, 0.05f, 3.0f);
        ImGui::SliderInt("SSR steps", &r.ssrSteps, 8, 128);
        ImGui::Checkbox("Height fog", &r.fog);
        if (r.fog) {
            ImGui::SliderFloat("Fog density", &r.fogDensity, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Fog falloff", &r.fogHeightFalloff, 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::DragFloat("Fog height", &r.fogHeight, 0.5f, -1000.0f, 1000.0f, "%.1f m");
        }
        ImGui::Checkbox("Specular anti-aliasing", &r.specularAntiAliasing);
        ImGui::Checkbox("Multi-scatter energy compensation", &r.multiScatter);
    }
    if (ImGui::CollapsingHeader("Level of detail")) {
        if (ImGui::SliderFloat("LOD bias", &state.lodBias, 0.1f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) engine.setLodBias(state.lodBias);
        if (ImGui::SliderInt("Forced LOD", &state.forcedLod, -1, 6, state.forcedLod < 0 ? "automatic" : "LOD %d")) engine.setForcedLod(state.forcedLod);
        if (ImGui::SliderFloat("Cull threshold", &state.smallObjectThreshold, 0.0f, 0.01f, "%.4f")) engine.setSmallObjectThreshold(state.smallObjectThreshold);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Projected radius below which objects are skipped. 0 keeps every object.");
    }
    if (ImGui::CollapsingHeader("Debug")) {
        int view = static_cast<int>(r.debugView);
        if (ImGui::Combo("View", &view, "Lit\0Base color\0Normals\0Roughness\0Metallic\0Ambient occlusion\0Sun shadows\0Reflections\0Lighting only\0Luminance heatmap\0Light clusters\0")) r.debugView = static_cast<DebugView>(view);
        if (ImGui::Checkbox("Hi-Z depth", &state.debugDepth)) engine.setDebugDepthMode(state.debugDepth);
        ImGui::Checkbox("Animate stress objects", &state.animateStress);
        ImGui::Checkbox("Grid", &ctx.showGrid);
        ImGui::Checkbox("Stats overlay", &ctx.showStats);
        if (ImGui::Button("Reload shaders (F9)")) engine.reloadShaders();
        for (const ShaderError& error : engine.shaderErrors()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s\n%s", error.source.c_str(), error.message.c_str());
    }
    if (ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen)) {
        float peak = 1.0f;
        for (float v : m_gpuHistory) peak = std::max(peak, v);
        ImGui::PlotLines("##gpu", m_gpuHistory.data(), static_cast<int>(m_gpuHistory.size()), static_cast<int>(m_historyHead), std::format("{:.2f} ms", gpu).c_str(), 0.0f, peak * 1.2f, ImVec2(-1.0f, 60.0f));
        if (ImGui::BeginTable("passes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            for (const GpuPassTiming& pass : engine.gpuTimings()) {
                if (pass.milliseconds < 0.005) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(std::string(pass.name).c_str());
                ImGui::TableSetColumnIndex(1);
                const float fraction = gpu > 0.0 ? static_cast<float>(pass.milliseconds / gpu) : 0.0f;
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), std::format("{:.3f} ms", pass.milliseconds).c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void LookPanel::drawWorld(EditorContext& ctx) {
    if (!ctx.selection.empty()) ctx.worldDismissed = false;
    m_worldVisible = !ctx.zen && (ctx.showWorld || (ctx.selection.empty() && ctx.showInspector && !ctx.showLook && !ctx.worldDismissed));
    if (!m_worldVisible) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = 330.0f;
    float right = viewport->WorkPos.x + viewport->WorkSize.x - 12.0f;
    for (const glm::vec4& panel : ctx.panels) {
        if (panel.x + panel.z > viewport->WorkPos.x + viewport->WorkSize.x * 0.5f) right = std::min(right, panel.x - 10.0f);
    }
    const float top = ctx.rightColumnTop;
    ImGui::SetNextWindowPos(ImVec2(right - width, top), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, 60.0f), ImVec2(width, std::max(200.0f, viewport->WorkPos.y + viewport->WorkSize.y - top - 44.0f)));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (!ImGui::Begin("World###WorldCard", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }
    if (!open) {
        ctx.showWorld = false;
        ctx.worldDismissed = true;
    }
    RenderSettings& r = ctx.engine.renderSettings();
    ImGui::TextDisabled("Nothing selected: editing the world. Shift+W toggles.");
    ImGui::PushItemWidth(-110.0f);
    if (ImGui::CollapsingHeader("Sky and sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        int sky = static_cast<int>(r.sky);
        if (ImGui::Combo("Sky", &sky, "Physical atmosphere\0Gradient\0HDRI\0")) r.sky = static_cast<SkyMode>(sky);
        sunDial("worldSun", r.timeOfDay, r.sunAzimuth);
        ImGui::SliderFloat("Time of day", &r.timeOfDay, 0.0f, 24.0f, "%.2f h");
        ImGui::SliderFloat("Sun azimuth", &r.sunAzimuth, 0.0f, 360.0f, "%.0f°");
        ImGui::DragFloat("Sun", &r.sunIlluminance, 500.0f, 0.0f, 200000.0f, "%.0f lux");
        ImGui::SliderFloat("Sky brightness", &r.skyBrightness, 0.0f, 4.0f, "%.2f");
        if (r.sky == SkyMode::Physical) {
            ImGui::Checkbox("Clouds", &r.clouds);
            if (r.clouds) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::SliderFloat("##coverage", &r.cloudCoverage, 0.0f, 1.0f, "coverage %.2f");
            }
        }
        if (r.sky == SkyMode::Gradient) {
            ImGui::ColorEdit3("Zenith", &r.gradientZenith.x);
            ImGui::ColorEdit3("Horizon", &r.gradientHorizon.x);
        }
        if (r.sky == SkyMode::Hdri) ImGui::InputTextWithHint("HDRI", "resources/sky.hdr", &r.hdriPath);
    }
    if (ImGui::CollapsingHeader("Light and exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Ambient (IBL)", &r.environmentIntensity, 0.0f, 4.0f);
        ImGui::Checkbox("Auto exposure", &r.autoExposure);
        ImGui::SameLine();
        ImGui::TextDisabled("EV100 %.1f", ctx.engine.exposureEv100());
        if (r.autoExposure) {
            ImGui::SliderFloat("Compensation", &r.exposureCompensation, -5.0f, 5.0f, "%+.1f EV");
        } else {
            ImGui::SliderFloat("EV100", &r.manualEV100, -6.0f, 18.0f, "%.1f");
        }
        int op = static_cast<int>(r.tonemapper);
        if (ImGui::Combo("Tonemapper", &op, "AgX\0AgX punchy\0ACES\0Khronos PBR Neutral\0Reinhard\0Linear\0")) r.tonemapper = static_cast<Tonemapper>(op);
    }
    if (ImGui::CollapsingHeader("Atmosphere and fog")) {
        ImGui::Checkbox("Height fog", &r.fog);
        if (r.fog) {
            ImGui::SliderFloat("Density", &r.fogDensity, 0.0f, 0.05f, "%.4f", ImGuiSliderFlags_Logarithmic);
            ImGui::DragFloat("Height", &r.fogHeight, 0.5f, -1000.0f, 1000.0f, "%.1f m");
        }
        if (r.sky == SkyMode::Physical) {
            ImGui::SliderFloat("Haze", &r.mieScale, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Aerial distance", &r.aerialDistanceScale, 0.0f, 20.0f);
        }
    }
    if (ImGui::CollapsingHeader("Effects")) {
        ImGui::Checkbox("Shadows", &r.shadows);
        ImGui::SameLine(120.0f);
        ImGui::Checkbox("AO", &r.ssao);
        ImGui::SameLine(200.0f);
        ImGui::Checkbox("SSR", &r.ssr);
        ImGui::Checkbox("Bloom", &r.bloom);
        ImGui::SameLine(120.0f);
        ImGui::Checkbox("FXAA", &r.fxaa);
    }
    ImGui::PopItemWidth();
    if (ImGui::SmallButton("All render settings (L)")) ctx.showLook = true;
    ImGui::End();
}

