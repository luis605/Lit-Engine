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

constexpr float PI = 3.14159f;
constexpr float TWO_PI = 6.28318f;
constexpr float DIAL_MAX_SIZE = 150.0f;
constexpr float DIAL_HEIGHT_RATIO = 0.62f;
constexpr float DIAL_CENTER_RATIO = 0.55f;
constexpr float DIAL_RADIUS_RATIO = 0.45f;
constexpr float SUNRISE_HOUR = 6.0f;
constexpr float SUNSET_HOUR = 18.0f;
constexpr float DAYLIGHT_HOURS = 12.0f;
constexpr float HOURS_PER_DAY = 24.0f;
constexpr float DEGREES_PER_TURN = 360.0f;
constexpr float PANEL_MARGIN = 10.0f;
constexpr float PANEL_WIDTH = 380.0f;
constexpr float PANEL_MIN_HEIGHT = 240.0f;
constexpr float PANEL_BOTTOM_MARGIN = 44.0f;
constexpr float PANEL_BACKGROUND_ALPHA = 0.93f;
constexpr double MIN_GPU_PASS_MILLISECONDS = 0.005;
constexpr float GPU_GRAPH_HEIGHT = 60.0f;
constexpr float GPU_GRAPH_HEADROOM = 1.2f;

}

bool LookPanel::sunDial(const char* id, float& hours, float& azimuth) {
    ImGui::PushID(id);
    const float size = std::min(ImGui::GetContentRegionAvail().x, DIAL_MAX_SIZE);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("dial", ImVec2(size, size * DIAL_HEIGHT_RATIO));
    const bool active = ImGui::IsItemActive();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * DIAL_CENTER_RATIO);
    const float radius = size * DIAL_RADIUS_RATIO;
    drawList->AddRectFilledMultiColor(origin, ImVec2(origin.x + size, center.y), IM_COL32(40, 70, 130, 255), IM_COL32(40, 70, 130, 255), IM_COL32(230, 150, 80, 255), IM_COL32(230, 150, 80, 255));
    drawList->AddRectFilled(ImVec2(origin.x, center.y), ImVec2(origin.x + size, origin.y + size * DIAL_HEIGHT_RATIO), IM_COL32(35, 32, 30, 255));
    drawList->PathArcTo(center, radius, PI, TWO_PI, 32);
    drawList->PathStroke(IM_COL32(255, 255, 255, 60), 0, 1.5f);
    const float dayFraction = std::clamp((hours - SUNRISE_HOUR) / DAYLIGHT_HOURS, -0.25f, 1.25f);
    const float angle = PI + dayFraction * PI;
    const ImVec2 sun(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius);
    const bool night = hours < SUNRISE_HOUR || hours > SUNSET_HOUR;
    drawList->AddCircleFilled(sun, 7.0f, night ? IM_COL32(180, 190, 220, 255) : IM_COL32(255, 220, 120, 255), 16);
    const std::string clock = std::format("{:02d}:{:02d}", static_cast<int>(hours) % 24, static_cast<int>(std::fmod(hours, 1.0f) * 60.0f));
    drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), IM_COL32(255, 255, 255, 220), clock.c_str());
    bool changed = false;
    if (active) {
        const ImGuiIO& io = ImGui::GetIO();
        const float mouseAngle = std::atan2(io.MousePos.y - center.y, io.MousePos.x - center.x);
        float fraction = (mouseAngle < 0.0f ? mouseAngle + TWO_PI : mouseAngle) / PI - 1.0f;
        if (io.MousePos.y > center.y) fraction = io.MousePos.x < center.x ? -0.1f : 1.1f;
        hours = std::clamp(SUNRISE_HOUR + fraction * DAYLIGHT_HOURS, 0.0f, HOURS_PER_DAY);
        if (io.KeyShift) {
            azimuth = std::fmod(azimuth + io.MouseDelta.x, DEGREES_PER_TURN);
            if (azimuth < 0.0f) azimuth += DEGREES_PER_TURN;
        }
        changed = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag the sun across the sky. Shift+drag spins its azimuth.");
    ImGui::PopID();
    return changed;
}

void LookPanel::drawSkyAndSun(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Sky and sun", ImGuiTreeNodeFlags_DefaultOpen)) return;
    RenderSettings& settings = ctx.engine.renderSettings();
    int sky = static_cast<int>(settings.sky);
    if (ImGui::Combo("Sky", &sky, "Physical atmosphere\0Gradient\0HDRI\0")) settings.sky = static_cast<SkyMode>(sky);
    sunDial("sun", settings.timeOfDay, settings.sunAzimuth);
    ImGui::SliderFloat("Time of day", &settings.timeOfDay, 0.0f, 24.0f, "%.2f h");
    ImGui::SliderFloat("Sun azimuth", &settings.sunAzimuth, 0.0f, 360.0f, "%.0f°");
    ImGui::Checkbox("Animate time", &settings.animateTime);
    if (settings.animateTime) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##speed", &settings.timeSpeed, 0.01f, 4.0f, "%.2f h/s", ImGuiSliderFlags_Logarithmic);
    }
    ImGui::Checkbox("Sun follows a Sun light entity", &settings.sunFollowsLight);
    ImGui::DragFloat("Sun illuminance", &settings.sunIlluminance, 500.0f, 0.0f, 200000.0f, "%.0f lux");
    ImGui::SliderFloat("Sun size", &settings.sunAngularDiameter, 0.1f, 5.0f, "%.2f°");
    if (settings.sky == SkyMode::Physical && ImGui::TreeNode("Atmosphere")) {
        ImGui::SliderFloat("Rayleigh", &settings.rayleighScale, 0.0f, 4.0f);
        ImGui::SliderFloat("Mie (haze)", &settings.mieScale, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Mie anisotropy", &settings.mieAnisotropy, 0.0f, 0.95f);
        ImGui::SliderFloat("Ozone", &settings.ozoneScale, 0.0f, 3.0f);
        ImGui::SliderFloat("Ground albedo", &settings.groundAlbedo, 0.0f, 1.0f);
        ImGui::SliderFloat("Aerial distance", &settings.aerialDistanceScale, 0.0f, 20.0f);
        ImGui::TreePop();
    }
    if (settings.sky == SkyMode::Physical && ImGui::TreeNode("Clouds and stars")) {
        ImGui::Checkbox("Clouds", &settings.clouds);
        ImGui::SliderFloat("Coverage", &settings.cloudCoverage, 0.0f, 1.0f);
        ImGui::SliderFloat("Density", &settings.cloudDensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Wind", &settings.cloudSpeed, 0.0f, 200.0f, "%.1f m/s");
        ImGui::DragFloat("Altitude", &settings.cloudAltitude, 10.0f, 200.0f, 12000.0f, "%.0f m");
        ImGui::Checkbox("Stars at night", &settings.stars);
        ImGui::Checkbox("Moon", &settings.moon);
        ImGui::SliderFloat("Moonlight", &settings.moonIlluminance, 0.0f, 10.0f, "%.2f lux");
        ImGui::Checkbox("God rays", &settings.godRays);
        ImGui::SliderFloat("God ray intensity", &settings.godRayIntensity, 0.0f, 4.0f);
        ImGui::TreePop();
    }
    if (settings.sky == SkyMode::Gradient) {
        ImGui::ColorEdit3("Zenith", &settings.gradientZenith.x);
        ImGui::ColorEdit3("Horizon", &settings.gradientHorizon.x);
        ImGui::ColorEdit3("Ground", &settings.gradientGround.x);
        ImGui::DragFloat("Brightness", &settings.gradientIntensity, 50.0f, 0.0f, 100000.0f, "%.0f nits");
    }
    if (settings.sky == SkyMode::Hdri) {
        ImGui::InputTextWithHint("HDRI file", "resources/sky.hdr", &settings.hdriPath);
        ImGui::DragFloat("Brightness", &settings.hdriIntensity, 50.0f, 0.0f, 200000.0f, "%.0f nits");
        ImGui::SliderFloat("Rotation", &settings.hdriRotation, 0.0f, 360.0f, "%.0f°");
    }
    ImGui::SliderFloat("Environment light", &settings.environmentIntensity, 0.0f, 4.0f);
    ImGui::SliderFloat("Sky brightness", &settings.skyBrightness, 0.0f, 16.0f);
}

void LookPanel::drawCameraAndExposure(EditorContext& ctx) {
    if (!ImGui::CollapsingHeader("Camera and exposure", ImGuiTreeNodeFlags_DefaultOpen)) return;
    Engine& engine = ctx.engine;
    RenderSettings& settings = engine.renderSettings();
    Camera& camera = ctx.world.camera();
    ImGui::Checkbox("Auto exposure", &settings.autoExposure);
    ImGui::SameLine();
    ImGui::TextDisabled("EV100 %.1f", engine.exposureEv100());
    if (settings.autoExposure) {
        ImGui::SliderFloat("Compensation", &settings.exposureCompensation, -5.0f, 5.0f, "%+.1f EV");
        ImGui::SliderFloat("Adapt brighter", &settings.adaptationUp, 0.1f, 10.0f);
        ImGui::SliderFloat("Adapt darker", &settings.adaptationDown, 0.1f, 10.0f);
        ImGui::DragFloatRange2("EV range", &settings.minEV100, &settings.maxEV100, 0.1f, -10.0f, 20.0f, "%.1f", "%.1f");
    } else {
        ImGui::SliderFloat("EV100", &settings.manualEV100, -6.0f, 18.0f, "%.1f");
        if (ImGui::Button("Sunny 16")) settings.manualEV100 = 15.0f;
        ImGui::SameLine();
        if (ImGui::Button("Overcast")) settings.manualEV100 = 12.0f;
        ImGui::SameLine();
        if (ImGui::Button("Indoor")) settings.manualEV100 = 7.0f;
        ImGui::SameLine();
        if (ImGui::Button("Night")) settings.manualEV100 = 0.0f;
    }
    float fov = camera.getFov();
    if (ImGui::SliderFloat("Field of view", &fov, 10.0f, 120.0f, "%.0f°")) camera.setFov(fov);
    float nearPlane = camera.getNearPlane();
    float farPlane = camera.getFarPlane();
    if (ImGui::DragFloat("Near", &nearPlane, 0.005f, 0.001f, 10.0f, "%.3f")) camera.setNearPlane(std::max(nearPlane, 0.001f));
    if (ImGui::DragFloat("Far", &farPlane, 5.0f, 10.0f, 1000000.0f, "%.0f")) camera.setFarPlane(std::max(farPlane, nearPlane + 1.0f));
    ImGui::SliderFloat("Fly speed", &ctx.flySpeed, 0.1f, 500.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
}

void LookPanel::drawGpu(EditorContext& ctx, double gpuMilliseconds) {
    if (!ImGui::CollapsingHeader("GPU", ImGuiTreeNodeFlags_DefaultOpen)) return;
    float peak = 1.0f;
    for (float sample : m_gpuHistory) peak = std::max(peak, sample);
    const std::string overlay = std::format("{:.2f} ms", gpuMilliseconds);
    ImGui::PlotLines("##gpu", m_gpuHistory.data(), static_cast<int>(m_gpuHistory.size()), static_cast<int>(m_historyHead), overlay.c_str(), 0.0f, peak * GPU_GRAPH_HEADROOM, ImVec2(-1.0f, GPU_GRAPH_HEIGHT));
    if (!ImGui::BeginTable("passes", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
    for (const GpuPassTiming& pass : ctx.engine.gpuTimings()) {
        if (pass.milliseconds < MIN_GPU_PASS_MILLISECONDS) continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(std::string(pass.name).c_str());
        ImGui::TableSetColumnIndex(1);
        const float fraction = gpuMilliseconds > 0.0 ? static_cast<float>(pass.milliseconds / gpuMilliseconds) : 0.0f;
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), std::format("{:.3f} ms", pass.milliseconds).c_str());
    }
    ImGui::EndTable();
}

void LookPanel::draw(EditorContext& ctx, LookState& state) {
    const double gpuMilliseconds = ctx.engine.gpuTotalMilliseconds();
    m_gpuHistory[m_historyHead] = static_cast<float>(gpuMilliseconds);
    m_historyHead = (m_historyHead + 1) % m_gpuHistory.size();
    if (!ctx.showLook || ctx.zen) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float top = ctx.rightColumnTop;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - PANEL_MARGIN, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(PANEL_WIDTH, std::max(PANEL_MIN_HEIGHT, viewport->WorkPos.y + viewport->WorkSize.y - top - PANEL_BOTTOM_MARGIN)), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(PANEL_BACKGROUND_ALPHA);
    if (!ImGui::Begin("Look###LookPanel", &ctx.showLook, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }
    ctx.panels.emplace_back(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
    ImGui::PushItemWidth(-120.0f);
    drawQuality(ctx);
    drawSkyAndSun(ctx);
    drawCameraAndExposure(ctx);
    drawColorGrading(ctx);
    drawShadows(ctx);
    drawScreenSpace(ctx);
    drawLevelOfDetail(ctx, state);
    drawDebug(ctx, state);
    drawGpu(ctx, gpuMilliseconds);
    ImGui::PopItemWidth();
    ImGui::End();
}
