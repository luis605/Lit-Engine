module;

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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

const glm::vec4 kAxisColors[3] = {glm::vec4(1.0f, 0.32f, 0.32f, 1.0f), glm::vec4(0.45f, 0.95f, 0.35f, 1.0f), glm::vec4(0.35f, 0.60f, 1.0f, 1.0f)};
const char* kAxisNames[3] = {"X", "Y", "Z"};

glm::mat4 translation(const glm::vec3& t) { return glm::translate(glm::mat4(1.0f), t); }

std::optional<glm::vec3> planeHit(const Ray& ray, const glm::vec3& point, const glm::vec3& normal) { return gizmo::rayPlane(ray, point, normal); }

float snapValue(float value, float step) { return step > 0.0f ? std::round(value / step) * step : value; }

}  // namespace

void TransformTool::capture(EditorContext& ctx) {
    m_starts.clear();
    glm::vec3 sum(0.0f);
    for (EntityHandle e : ctx.topLevelSelection()) {
        const EntityHandle parent = ctx.world.getParent(e);
        m_starts.push_back({e, ctx.world.getLocalMatrix(e), ctx.world.getWorldMatrix(e), parent.isNull() ? glm::mat4(1.0f) : ctx.world.getWorldMatrix(parent)});
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
        for (const Start& s : m_starts) ctx.world.setLocalMatrix(s.entity, s.local);
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
    m_lastAngleVector = glm::normalize(ctx.mouse - m_pivotScreen + glm::vec2(1e-3f, 0.0f));
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    const glm::vec3 viewNormal = cameraForward(ctx.world.camera());
    m_startHit = planeHit(ray, m_pivot, viewNormal).value_or(m_pivot);
    m_startParameter = 0.0f;
    ctx.modalActive = true;
}

glm::vec3 TransformTool::axisDirection(EditorContext& ctx) const {
    if (m_axis < 0) return cameraForward(ctx.world.camera());
    glm::vec3 axis(0.0f);
    axis[m_axis] = 1.0f;
    if (m_localAxis && !m_starts.empty()) axis = glm::normalize(glm::vec3(m_starts.back().world[m_axis]));
    return axis;
}

std::string TransformTool::status() const {
    const char* verb = m_kind == ModalKind::Move ? "Move" : m_kind == ModalKind::Rotate ? "Rotate" : "Scale";
    std::string axis = m_axis < 0 ? (m_kind == ModalKind::Move ? "view plane" : m_kind == ModalKind::Rotate ? "view axis" : "uniform") : std::string(m_plane ? "plane " : "") + kAxisNames[m_axis] + (m_localAxis ? " local" : "");
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
    return std::format("{}  {}  {}", verb, axis, value);
}

void TransformTool::apply(EditorContext& ctx) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool snap = ctx.snapping != io.KeyCtrl;
    const std::optional<double> typed = m_typed.empty() ? std::nullopt : evaluateExpression(m_typed);
    const Ray ray = ctx.engine.screenRay(ctx.mouse.x, ctx.mouse.y);
    const glm::vec3 axis = axisDirection(ctx);

    if (m_kind == ModalKind::Move) {
        glm::vec3 delta(0.0f);
        if (typed) {
            glm::vec3 dir = m_axis < 0 ? glm::vec3(1.0f, 0.0f, 0.0f) : axis;
            delta = dir * static_cast<float>(*typed);
        } else if (m_axis >= 0 && !m_plane) {
            const float t = gizmo::closestParameterOnAxis(ray, m_pivot, axis);
            const Ray startRay = ctx.engine.screenRay(m_startMouse.x, m_startMouse.y);
            const float t0 = gizmo::closestParameterOnAxis(startRay, m_pivot, axis);
            float d = t - t0;
            if (snap) d = snapValue(d, ctx.snapMove);
            delta = axis * d;
        } else {
            const glm::vec3 normal = m_axis >= 0 ? axis : cameraForward(ctx.world.camera());
            const Ray startRay = ctx.engine.screenRay(m_startMouse.x, m_startMouse.y);
            const std::optional<glm::vec3> a = planeHit(startRay, m_pivot, normal);
            const std::optional<glm::vec3> b = planeHit(ray, m_pivot, normal);
            if (a && b) delta = *b - *a;
            if (snap) delta = glm::vec3(snapValue(delta.x, ctx.snapMove), snapValue(delta.y, ctx.snapMove), snapValue(delta.z, ctx.snapMove));
        }
        m_delta = delta;
        for (const Start& s : m_starts) {
            const glm::mat4 world = translation(delta) * s.world;
            ctx.world.setLocalMatrix(s.entity, glm::inverse(s.parentWorld) * world);
        }
        return;
    }

    if (m_kind == ModalKind::Rotate) {
        const glm::vec2 current = ctx.mouse - m_pivotScreen;
        if (glm::length(current) > 2.0f) {
            const glm::vec2 v = glm::normalize(current);
            const float step = std::atan2(m_lastAngleVector.x * v.y - m_lastAngleVector.y * v.x, glm::dot(m_lastAngleVector, v));
            if (std::isfinite(step)) m_accumulatedAngle += step;
            m_lastAngleVector = v;
        }
        const glm::vec3 viewDir = cameraForward(ctx.world.camera());
        const float facing = glm::dot(axis, viewDir) >= 0.0f ? 1.0f : -1.0f;
        float degrees = typed ? static_cast<float>(*typed) : glm::degrees(m_accumulatedAngle) * facing;
        if (snap && !typed) degrees = snapValue(degrees, ctx.snapAngle);
        m_value = degrees;
        const glm::mat4 rotation = glm::translate(glm::mat4(1.0f), m_pivot) * glm::mat4_cast(glm::angleAxis(glm::radians(typed ? degrees : -degrees), axis)) * glm::translate(glm::mat4(1.0f), -m_pivot);
        for (const Start& s : m_starts) ctx.world.setLocalMatrix(s.entity, glm::inverse(s.parentWorld) * rotation * s.world);
        return;
    }

    const float startDistance = std::max(glm::length(m_startMouse - m_pivotScreen), 4.0f);
    float factor = typed ? static_cast<float>(*typed) : glm::length(ctx.mouse - m_pivotScreen) / startDistance;
    if (snap && !typed) factor = std::max(snapValue(factor, ctx.snapScale), ctx.snapScale);
    m_value = factor;
    for (const Start& s : m_starts) {
        glm::vec3 offset = glm::vec3(s.world[3]) - m_pivot;
        glm::mat4 world = s.world;
        if (m_axis < 0) {
            offset *= factor;
            world[0] *= factor;
            world[1] *= factor;
            world[2] *= factor;
        } else {
            const glm::vec3 a = axis;
            offset += a * (glm::dot(offset, a) * (factor - 1.0f));
            for (int c = 0; c < 3; ++c) {
                const glm::vec3 column(world[c]);
                const float along = glm::length(column) > 1e-6f ? std::abs(glm::dot(glm::normalize(column), a)) : 0.0f;
                world[c] = glm::vec4(column * (1.0f + (factor - 1.0f) * along), 0.0f);
            }
        }
        world[3] = glm::vec4(m_pivot + offset, 1.0f);
        ctx.world.setLocalMatrix(s.entity, glm::inverse(s.parentWorld) * world);
    }
}

void TransformTool::commit(EditorContext& ctx, const char* label) {
    ctx.history->beginGroup(label);
    for (const Start& s : m_starts) {
        if (!ctx.world.isAlive(s.entity)) continue;
        ctx.history->commitLocalMatrix(s.entity, s.local, ctx.world.getLocalMatrix(s.entity));
    }
    ctx.history->endGroup();
}

void TransformTool::cancel(EditorContext& ctx) {
    for (const Start& s : m_starts) {
        if (ctx.world.isAlive(s.entity)) ctx.world.setLocalMatrix(s.entity, s.local);
    }
}

void TransformTool::update(EditorContext& ctx) {
    if (m_kind == ModalKind::None) return;
    const ImGuiIO& io = ImGui::GetIO();
    auto axisKey = [&](ImGuiKey key, int axis) {
        if (!ImGui::IsKeyPressed(key, false)) return;
        if (m_axis == axis && m_plane == io.KeyShift) {
            if (m_localAxis) {
                m_axis = -1;
                m_localAxis = false;
            } else {
                m_localAxis = true;
            }
        } else {
            m_axis = axis;
            m_plane = io.KeyShift;
            m_localAxis = ctx.localSpace;
        }
    };
    axisKey(ImGuiKey_X, 0);
    axisKey(ImGuiKey_Y, 1);
    axisKey(ImGuiKey_Z, 2);
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) begin(ctx, ModalKind::Move);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) begin(ctx, ModalKind::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_S, false)) begin(ctx, ModalKind::Scale);

    for (ImWchar c : io.InputQueueCharacters) {
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')' || c == '^' || c == 'p' || c == 'i') m_typed.push_back(static_cast<char>(c));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (!m_typed.empty()) m_typed.pop_back();
    }

    apply(ctx);

    const bool confirm = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool abort = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const char* label = m_kind == ModalKind::Move ? "Move" : m_kind == ModalKind::Rotate ? "Rotate" : "Scale";
    ctx.modeLabel = label;
    ctx.hint("X/Y/Z", "axis (again: local)");
    ctx.hint("Shift+X/Y/Z", "plane");
    ctx.hint("0-9 . -", "type value");
    ctx.hint("Ctrl", ctx.snapping ? "free" : "snap");
    ctx.hint("Enter/Click", "apply");
    ctx.hint("Esc/RMB", "cancel");

    const glm::vec3 axis = axisDirection(ctx);
    if (m_axis >= 0) {
        const glm::vec4 color = kAxisColors[m_axis];
        if (m_plane) {
            for (int other = 0; other < 3; ++other) {
                if (other == m_axis) continue;
                glm::vec3 dir(0.0f);
                dir[other] = 1.0f;
                ctx.engine.debugLine(m_pivot - dir * 2000.0f, m_pivot + dir * 2000.0f, kAxisColors[other], true);
            }
        } else {
            ctx.engine.debugLine(m_pivot - axis * 2000.0f, m_pivot + axis * 2000.0f, color, true);
        }
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (m_kind != ModalKind::Move) draw->AddLine(ImVec2(m_pivotScreen.x, m_pivotScreen.y), ImVec2(ctx.mouse.x, ctx.mouse.y), IM_COL32(255, 190, 90, 160), 1.5f);
    const std::string text = status();
    const ImVec2 at(ctx.mouse.x + 18.0f, ctx.mouse.y + 18.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    draw->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 5.0f), ImVec2(at.x + textSize.x + 8.0f, at.y + textSize.y + 5.0f), IM_COL32(18, 20, 24, 225), 6.0f);
    draw->AddText(at, IM_COL32(255, 200, 110, 255), text.c_str());

    if (confirm) {
        commit(ctx, label);
        m_kind = ModalKind::None;
        ctx.modalActive = false;
    } else if (abort) {
        cancel(ctx);
        m_kind = ModalKind::None;
        ctx.modalActive = false;
    }
}

void TransformTool::drawHandles(EditorContext& ctx) {
    const EntityHandle primary = ctx.primary();
    if (ctx.handles == HandleMode::Off || m_kind != ModalKind::None || !ctx.world.isAlive(primary) || ctx.flying) {
        m_handleDragging = false;
        return;
    }
    const glm::vec2 size = ctx.engine.windowSize();
    const Camera& camera = ctx.world.camera();
    ImGuizmo::SetOrthographic(camera.isOrthographic());
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, size.x, size.y);
    ImGuizmo::AllowAxisFlip(false);
    const glm::mat4 view = camera.getViewMatrix();
    const float aspect = size.x / std::max(size.y, 1.0f);
    const float halfHeight = camera.getOrthoHeight() * 0.5f;
    const glm::mat4 projection = camera.isOrthographic() ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, camera.getNearPlane(), camera.getFarPlane()) : glm::perspective(glm::radians(camera.getFov()), aspect, camera.getNearPlane(), camera.getFarPlane());
    glm::mat4 matrix = ctx.world.getWorldMatrix(primary);
    const ImGuizmo::OPERATION op = ctx.handles == HandleMode::Translate ? ImGuizmo::TRANSLATE : ctx.handles == HandleMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    const ImGuizmo::MODE mode = ctx.localSpace || ctx.handles == HandleMode::Scale ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    const bool snap = ctx.snapping != ImGui::GetIO().KeyCtrl;
    const float step = ctx.handles == HandleMode::Translate ? ctx.snapMove : ctx.handles == HandleMode::Rotate ? ctx.snapAngle : ctx.snapScale;
    const float snapValues[3] = {step, step, step};
    const bool changed = ImGuizmo::Manipulate(&view[0][0], &projection[0][0], op, mode, &matrix[0][0], nullptr, snap ? snapValues : nullptr);
    const bool using_ = ImGuizmo::IsUsing();
    if (using_ && !m_handleDragging) {
        m_handleDragging = true;
        capture(ctx);
        m_handleStarts = m_starts;
        m_handlePrimaryStart = ctx.world.getWorldMatrix(primary);
    }
    if (m_handleDragging && changed) {
        const glm::mat4 delta = matrix * glm::inverse(m_handlePrimaryStart);
        for (const Start& s : m_handleStarts) {
            const glm::mat4 world = s.entity == primary ? matrix : delta * s.world;
            ctx.world.setLocalMatrix(s.entity, glm::inverse(s.parentWorld) * world);
        }
    }
    if (!using_ && m_handleDragging) {
        m_handleDragging = false;
        m_starts = m_handleStarts;
        commit(ctx, ctx.handles == HandleMode::Translate ? "Move" : ctx.handles == HandleMode::Rotate ? "Rotate" : "Scale");
    }
}

void TransformTool::nudge(EditorContext& ctx, const glm::vec3& screenDirection) {
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    const Camera& camera = ctx.world.camera();
    const glm::vec3 forward = cameraForward(camera);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    auto dominant = [](glm::vec3 v) {
        v.y = 0.0f;
        if (std::abs(v.x) >= std::abs(v.z)) return glm::vec3(v.x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
        return glm::vec3(0.0f, 0.0f, v.z >= 0.0f ? 1.0f : -1.0f);
    };
    const glm::vec3 dir = dominant(right) * screenDirection.x + dominant(forward) * screenDirection.z + glm::vec3(0.0f, screenDirection.y, 0.0f);
    const float step = ctx.snapMove * (ImGui::GetIO().KeyShift ? 10.0f : 1.0f);
    ctx.history->beginGroup("Nudge");
    for (EntityHandle e : top) {
        const EntityHandle parent = ctx.world.getParent(e);
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : ctx.world.getWorldMatrix(parent);
        const glm::mat4 world = translation(dir * step) * ctx.world.getWorldMatrix(e);
        ctx.history->setLocalMatrix(e, glm::inverse(parentWorld) * world);
    }
    ctx.history->endGroup();
}

void TransformTool::resetTransform(EditorContext& ctx, int part) {
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    ctx.history->beginGroup(part == 0 ? "Reset position" : part == 1 ? "Reset rotation" : "Reset scale");
    for (EntityHandle e : top) {
        const glm::vec3 position = ctx.world.getPosition(e);
        const glm::quat rotation = ctx.world.getRotation(e);
        const glm::vec3 scale = ctx.world.getScale(e);
        const glm::vec3 p = part == 0 ? glm::vec3(0.0f) : position;
        const glm::quat r = part == 1 ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : rotation;
        const glm::vec3 s = part == 2 ? glm::vec3(1.0f) : scale;
        ctx.history->setLocalMatrix(e, glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s));
    }
    ctx.history->endGroup();
}
