module;

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

module Editor.context;

import Engine.engine;
import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

std::string displayName(World& world, EntityHandle e) {
    if (!world.isAlive(e)) return "(none)";
    const std::string& name = world.getName(e);
    if (!name.empty()) return name;
    return std::format("Entity {}", e.index);
}

std::string meshLabel(Engine& engine, uint32_t mesh) {
    if (mesh == NO_MESH) return "empty";
    const std::string name = engine.meshName(mesh);
    return name.empty() ? std::format("mesh {}", mesh) : name;
}

glm::vec3 cameraForward(const Camera& camera) {
    const float yaw = glm::radians(camera.getYaw());
    const float pitch = glm::radians(camera.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

void flyTo(EditorContext& ctx, const glm::vec4& sphere) {
    Camera& camera = ctx.world.camera();
    const glm::vec3 forward = cameraForward(camera);
    const float radius = std::max(sphere.w, 0.25f);
    const float halfFov = glm::radians(camera.getFov()) * 0.5f;
    const glm::vec2 size = ctx.engine.windowSize();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
    const float fit = std::min(halfFov, std::atan(std::tan(halfFov) * aspect));
    const float distance = radius / std::sin(fit) * 1.15f;
    ctx.flight.active = true;
    ctx.flight.t = 0.0f;
    ctx.flight.from = camera.getPosition();
    ctx.flight.to = glm::vec3(sphere) - forward * distance;
    ctx.flight.fromYaw = ctx.flight.toYaw = camera.getYaw();
    ctx.flight.fromPitch = ctx.flight.toPitch = camera.getPitch();
    if (distance * 2.0f > camera.getFarPlane()) camera.setFarPlane(distance * 2.0f);
}

void updateFlight(EditorContext& ctx, float dt) {
    if (!ctx.flight.active) return;
    CameraFlight& f = ctx.flight;
    f.t = std::min(1.0f, f.t + dt / std::max(f.duration, 1e-3f));
    const float k = 1.0f - std::pow(1.0f - f.t, 3.0f);
    Camera& camera = ctx.world.camera();
    camera.setPos(f.from + (f.to - f.from) * k);
    float yawDelta = std::fmod(f.toYaw - f.fromYaw + 540.0f, 360.0f) - 180.0f;
    camera.setOrientation(f.fromYaw + yawDelta * k, f.fromPitch + (f.toPitch - f.fromPitch) * k);
    if (f.t >= 1.0f) f.active = false;
}

void applyEditorTheme(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 8.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.DisabledAlpha = 0.45f;
    (void)scale;

    ImVec4* c = style.Colors;
    const ImVec4 bg(0.075f, 0.080f, 0.092f, 0.94f);
    const ImVec4 panel(0.105f, 0.112f, 0.128f, 1.0f);
    const ImVec4 frame(0.150f, 0.160f, 0.182f, 1.0f);
    const ImVec4 frameHover(0.200f, 0.212f, 0.240f, 1.0f);
    const ImVec4 accent(1.000f, 0.690f, 0.260f, 1.0f);
    const ImVec4 accentDim(1.000f, 0.690f, 0.260f, 0.55f);
    const ImVec4 accentFaint(1.000f, 0.690f, 0.260f, 0.22f);
    const ImVec4 text(0.910f, 0.918f, 0.930f, 1.0f);
    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = ImVec4(0.520f, 0.540f, 0.580f, 1.0f);
    c[ImGuiCol_WindowBg] = bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.095f, 0.110f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = frame;
    c[ImGuiCol_FrameBgHovered] = frameHover;
    c[ImGuiCol_FrameBgActive] = ImVec4(0.240f, 0.252f, 0.284f, 1.0f);
    c[ImGuiCol_TitleBg] = panel;
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_TitleBgCollapsed] = panel;
    c[ImGuiCol_MenuBarBg] = panel;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.22f);
    c[ImGuiCol_ScrollbarGrabActive] = accentDim;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accentDim;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = frame;
    c[ImGuiCol_ButtonHovered] = frameHover;
    c[ImGuiCol_ButtonActive] = accentDim;
    c[ImGuiCol_Header] = accentFaint;
    c[ImGuiCol_HeaderHovered] = ImVec4(1.0f, 0.690f, 0.260f, 0.30f);
    c[ImGuiCol_HeaderActive] = accentDim;
    c[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    c[ImGuiCol_SeparatorHovered] = accentDim;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    c[ImGuiCol_ResizeGripHovered] = accentDim;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = panel;
    c[ImGuiCol_TabHovered] = frameHover;
    c[ImGuiCol_TabSelected] = frame;
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TabDimmed] = panel;
    c[ImGuiCol_TabDimmedSelected] = frame;
    c[ImGuiCol_DockingPreview] = accentDim;
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = panel;
    c[ImGuiCol_TableBorderStrong] = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
    c[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_TextSelectedBg] = accentFaint;
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavCursor] = accent;
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}

namespace {

struct ExpressionParser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    double primary() {
        skip();
        if (i >= s.size()) {
            ok = false;
            return 0.0;
        }
        if (s[i] == '(') {
            ++i;
            const double v = sum();
            skip();
            if (i < s.size() && s[i] == ')') {
                ++i;
            } else {
                ok = false;
            }
            return v;
        }
        if (s[i] == '-') {
            ++i;
            return -power();
        }
        if (s[i] == '+') {
            ++i;
            return power();
        }
        if (std::isalpha(static_cast<unsigned char>(s[i]))) {
            size_t start = i;
            while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            const std::string word = s.substr(start, i - start);
            if (word == "pi") return 3.14159265358979323846;
            if (word == "tau") return 6.28318530717958647692;
            if (word == "e") return 2.71828182845904523536;
            skip();
            if (i < s.size() && s[i] == '(') {
                ++i;
                const double v = sum();
                skip();
                if (i < s.size() && s[i] == ')') ++i;
                if (word == "sin") return std::sin(v);
                if (word == "cos") return std::cos(v);
                if (word == "tan") return std::tan(v);
                if (word == "sqrt") return std::sqrt(v);
                if (word == "abs") return std::abs(v);
                if (word == "rad") return v * 3.14159265358979323846 / 180.0;
                if (word == "deg") return v * 180.0 / 3.14159265358979323846;
            }
            ok = false;
            return 0.0;
        }
        size_t used = 0;
        double v = 0.0;
        try {
            v = std::stod(s.substr(i), &used);
        } catch (...) {
            ok = false;
            return 0.0;
        }
        i += used;
        return v;
    }

    double power() {
        double base = primary();
        skip();
        if (i < s.size() && s[i] == '^') {
            ++i;
            return std::pow(base, power());
        }
        return base;
    }

    double product() {
        double v = power();
        while (ok) {
            skip();
            if (i >= s.size()) break;
            const char op = s[i];
            if (op != '*' && op != '/' && op != '%') break;
            ++i;
            const double r = power();
            if (op == '*') v *= r;
            if (op == '/') v = r == 0.0 ? 0.0 : v / r;
            if (op == '%') v = r == 0.0 ? 0.0 : std::fmod(v, r);
        }
        return v;
    }

    double sum() {
        double v = product();
        while (ok) {
            skip();
            if (i >= s.size()) break;
            const char op = s[i];
            if (op != '+' && op != '-') break;
            ++i;
            const double r = product();
            v = op == '+' ? v + r : v - r;
        }
        return v;
    }
};

}  // namespace

std::optional<double> evaluateExpression(const std::string& text) {
    if (text.empty()) return std::nullopt;
    ExpressionParser parser{text};
    const double v = parser.sum();
    parser.skip();
    if (!parser.ok || parser.i != text.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

float fuzzyScore(const std::string& pattern, const std::string& text) {
    if (pattern.empty()) return 1.0f;
    float score = 0.0f;
    size_t p = 0;
    int streak = 0;
    char previous = ' ';
    for (size_t t = 0; t < text.size() && p < pattern.size(); ++t) {
        const char tc = static_cast<char>(std::tolower(static_cast<unsigned char>(text[t])));
        const char pc = static_cast<char>(std::tolower(static_cast<unsigned char>(pattern[p])));
        if (tc == pc) {
            float bonus = 1.0f;
            const bool wordStart = t == 0 || previous == ' ' || previous == '_' || previous == '-' || previous == '/' || previous == '.' || (std::isupper(static_cast<unsigned char>(text[t])) && std::islower(static_cast<unsigned char>(previous)));
            if (wordStart) bonus += 2.0f;
            bonus += static_cast<float>(streak) * 1.5f;
            score += bonus;
            ++streak;
            ++p;
        } else {
            streak = 0;
        }
        previous = text[t];
    }
    if (p < pattern.size()) return 0.0f;
    return score / (1.0f + 0.02f * static_cast<float>(text.size()));
}
