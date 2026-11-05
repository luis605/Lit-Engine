module;

#include <imgui.h>

#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

module Editor.gizmo;

import Editor.context;
import Engine.engine;
import Engine.World;
import Engine.History;
import Engine.GizmoMath;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;

namespace {

const glm::vec4 AXIS_COLORS[3] = {glm::vec4(1.0f, 0.32f, 0.32f, 1.0f), glm::vec4(0.45f, 0.95f, 0.35f, 1.0f), glm::vec4(0.35f, 0.60f, 1.0f, 1.0f)};
const char* const AXIS_NAMES[3] = {"X", "Y", "Z"};
constexpr float GUIDE_LENGTH = 2000.0f;
constexpr float MIN_AXIS_LENGTH = 1e-6f;
constexpr float ANGLE_EPSILON = 1e-3f;
constexpr float OVERLAY_OFFSET = 18.0f;

bool isExpressionCharacter(ImWchar character) {
    return (character >= '0' && character <= '9') || character == '.' || character == '-' || character == '+' || character == '*' || character == '/' || character == '(' || character == ')' || character == '^' || character == 'p' || character == 'i';
}

}

const char* TransformTool::kindLabel() const { return m_kind == ModalKind::Move ? "Move" : m_kind == ModalKind::Rotate ? "Rotate" : "Scale"; }

void TransformTool::capture(EditorContext& ctx) {
    m_starts.clear();
    glm::vec3 sum(0.0f);
    for (EntityHandle entity : ctx.topLevelSelection()) {
        const EntityHandle parent = ctx.world.getParent(entity);
        m_starts.push_back({entity, ctx.world.getLocalMatrix(entity), ctx.world.getWorldMatrix(entity), parent.isNull() ? glm::mat4(1.0f) : ctx.world.getWorldMatrix(parent)});
        sum += glm::vec3(m_starts.back().world[3]);
    }
    m_pivot = m_starts.empty() ? glm::vec3(0.0f) : sum / static_cast<float>(m_starts.size());
}

void TransformTool::begin(EditorContext& ctx, ModalKind kind) {
    if (ctx.selection.empty()) {
        ctx.toast("Nothing selected", ToastKind::Warning);
        return;
    }
    if (m_kind != ModalKind::None) {
        for (const Start& start : m_starts) ctx.world.setLocalMatrix(start.entity, start.local);
    } else {
        capture(ctx);
        m_axis = -1;
        m_plane = false;
        m_localAxis = false;
    }
    if (m_starts.empty()) return;
    m_kind = kind;
    m_typed.clear();
    m_startMouse = ctx.mouse;
    m_accumulatedAngle = 0.0f;
    const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(m_pivot);
    m_pivotScreen = screen && std::isfinite(screen->x) && std::isfinite(screen->y) ? *screen : ctx.mouse;
    m_lastAngleVector = glm::normalize(ctx.mouse - m_pivotScreen + glm::vec2(ANGLE_EPSILON, 0.0f));
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    m_startHit = gizmo::rayPlane(ray, m_pivot, cameraForward(ctx.world.camera())).value_or(m_pivot);
    m_startParameter = 0.0f;
    ctx.modalActive = true;
}

glm::vec3 TransformTool::axisDirection(EditorContext& ctx) const {
    if (m_axis < 0) return cameraForward(ctx.world.camera());
    glm::vec3 axis(0.0f);
    axis[m_axis] = 1.0f;
    if (m_localAxis && !m_starts.empty()) {
        const glm::vec3 column(m_starts.back().world[m_axis]);
        if (glm::length(column) > MIN_AXIS_LENGTH) axis = glm::normalize(column);
    }
    return axis;
}

std::string TransformTool::status() const {
    std::string axis;
    if (m_axis < 0) {
        axis = m_kind == ModalKind::Move ? "view plane" : m_kind == ModalKind::Rotate ? "view axis" : "uniform";
    } else {
        axis = std::string(m_plane ? "plane " : "") + AXIS_NAMES[m_axis] + (m_localAxis ? " local" : "");
    }
    std::string value;
    if (!m_typed.empty()) {
        value = "= " + m_typed + "|";
    } else if (m_kind == ModalKind::Move) {
        value = std::format("{:.3f}, {:.3f}, {:.3f}", m_delta.x, m_delta.y, m_delta.z);
    } else if (m_kind == ModalKind::Rotate) {
        value = std::format("{:.1f}°", m_value);
    } else {
        value = std::format("×{:.3f}", m_value);
    }
    return std::format("{}  {}  {}", kindLabel(), axis, value);
}

void TransformTool::commit(EditorContext& ctx, const char* label) {
    ctx.history->beginGroup(label);
    for (const Start& start : m_starts) {
        if (!ctx.world.isAlive(start.entity)) continue;
        ctx.history->commitLocalMatrix(start.entity, start.local, ctx.world.getLocalMatrix(start.entity));
    }
    ctx.history->endGroup();
}

void TransformTool::cancel(EditorContext& ctx) {
    for (const Start& start : m_starts) {
        if (ctx.world.isAlive(start.entity)) ctx.world.setLocalMatrix(start.entity, start.local);
    }
}

void TransformTool::handleAxisKeys(EditorContext& ctx) {
    const bool shift = ImGui::GetIO().KeyShift;
    auto axisKey = [&](ImGuiKey key, int axis) {
        if (!ImGui::IsKeyPressed(key, false)) return;
        if (m_axis != axis || m_plane != shift) {
            m_axis = axis;
            m_plane = shift;
            m_localAxis = ctx.localSpace;
        } else if (m_localAxis) {
            m_axis = -1;
            m_localAxis = false;
        } else {
            m_localAxis = true;
        }
    };
    axisKey(ImGuiKey_X, 0);
    axisKey(ImGuiKey_Y, 1);
    axisKey(ImGuiKey_Z, 2);
}

void TransformTool::handleTypedInput() {
    for (ImWchar character : ImGui::GetIO().InputQueueCharacters) {
        if (isExpressionCharacter(character)) m_typed.push_back(static_cast<char>(character));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !m_typed.empty()) m_typed.pop_back();
}

void TransformTool::drawAxisGuides(EditorContext& ctx) {
    if (m_axis < 0) return;
    if (!m_plane) {
        const glm::vec3 axis = axisDirection(ctx);
        ctx.engine.debugLine(m_pivot - axis * GUIDE_LENGTH, m_pivot + axis * GUIDE_LENGTH, AXIS_COLORS[m_axis], true);
        return;
    }
    for (int other = 0; other < 3; ++other) {
        if (other == m_axis) continue;
        glm::vec3 direction(0.0f);
        direction[other] = 1.0f;
        ctx.engine.debugLine(m_pivot - direction * GUIDE_LENGTH, m_pivot + direction * GUIDE_LENGTH, AXIS_COLORS[other], true);
    }
}

void TransformTool::drawModalOverlay(EditorContext& ctx) {
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (m_kind != ModalKind::Move) drawList->AddLine(ImVec2(m_pivotScreen.x, m_pivotScreen.y), ImVec2(ctx.mouse.x, ctx.mouse.y), IM_COL32(255, 190, 90, 160), 1.5f);
    const std::string text = status();
    const ImVec2 at(ctx.mouse.x + OVERLAY_OFFSET, ctx.mouse.y + OVERLAY_OFFSET);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    drawList->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 5.0f), ImVec2(at.x + textSize.x + 8.0f, at.y + textSize.y + 5.0f), IM_COL32(18, 20, 24, 225), 6.0f);
    drawList->AddText(at, IM_COL32(255, 200, 110, 255), text.c_str());
}

void TransformTool::update(EditorContext& ctx) {
    if (m_kind == ModalKind::None) return;
    handleAxisKeys(ctx);
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) begin(ctx, ModalKind::Move);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) begin(ctx, ModalKind::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) begin(ctx, ModalKind::Scale);
    handleTypedInput();
    apply(ctx);

    const bool confirm = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool abort = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const char* label = kindLabel();
    ctx.modeLabel = label;
    ctx.hint("X/Y/Z", "axis (again: local)");
    ctx.hint("Shift+X/Y/Z", "plane");
    ctx.hint("0-9 . -", "type value");
    ctx.hint("Ctrl", ctx.snapping ? "free" : "snap");
    ctx.hint("Enter/Click", "apply");
    ctx.hint("Esc/RMB", "cancel");

    drawAxisGuides(ctx);
    drawModalOverlay(ctx);

    if (!confirm && !abort) return;
    if (confirm) {
        commit(ctx, label);
    } else {
        cancel(ctx);
    }
    m_kind = ModalKind::None;
    ctx.modalActive = false;
}
