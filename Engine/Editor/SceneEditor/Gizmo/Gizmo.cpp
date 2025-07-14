/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Editor/SceneEditor/Gizmo/Gizmo.hpp>
#include <Engine/Core/Engine.hpp>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Core/Math.hpp>
#include <Engine/Editor/MenuBar/Settings.hpp>

#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>

Gizmo editorGizmo;

const std::array<Vector3, Gizmo::AXIS_DIRECTIONS_COUNT> Gizmo::AXIS_DIRECTIONS = {{
    { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f }, {  0.0f, -1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }, {  0.0f,  0.0f, -1.0f }
}};

const std::array<Color, Gizmo::AXIS_COUNT> Gizmo::AXIS_BASE_COLORS = {{
    {210,  50,  50, 255},
    { 50, 210,  50, 255},
    { 50,  50, 210, 255}
}};

const std::array<Color, Gizmo::AXIS_COUNT> Gizmo::AXIS_HIGHLIGHT_COLORS = {{
    {255,  80,  80, 255},
    { 80, 255,  80, 255},
    { 80,  80, 255, 255}
}};

const std::array<Vector3, Gizmo::AXIS_COUNT> Gizmo::ROTATION_AXES = {{
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }
}};

const std::array<Vector3, Gizmo::AXIS_COUNT> Gizmo::ROTATION_DIRECTIONS = {{
    { 0.0f, 90.0f, 0.0f },
    { 90.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 90.0f }
}};

void Gizmo::beginFrame() {
    rlSetLineWidth(AXIS_LINE_WIDTH);

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        endDrag();
    }
}

void Gizmo::endFrame() {
    rlDrawRenderBatchActive();
    rlSetLineWidth(DEFAULT_LINE_WIDTH);
}

void Gizmo::updateAndDraw(Type type, Vector3& objPosition, Vector3& objScale, Quaternion& objRotation, const LitCamera& camera, const Rectangle& viewport) {
    const Vector2 mousePosition = GetMousePosition();
    const Vector2 relativeMousePos = {
        .x = mousePosition.x - viewport.x,
        .y = mousePosition.y - viewport.y
    };
    const Ray mouseRay = GetScreenToWorldRayEx(relativeMousePos, camera, viewport.width, viewport.height);

    switch (type) {
        case Type::Position:
            updateState(Type::Position, objPosition, objPosition, camera, mouseRay);
            drawPositionGizmo(objPosition, camera);
            break;
        case Type::Scale:
            updateState(Type::Scale, objScale, objPosition, camera, mouseRay);
            drawScaleGizmo(objPosition, camera);
            objScale.x = std::max(MINIMUM_LEGAL_SCALE, objScale.x);
            objScale.y = std::max(MINIMUM_LEGAL_SCALE, objScale.y);
            objScale.z = std::max(MINIMUM_LEGAL_SCALE, objScale.z);
            break;
        case Type::Rotation:
            updateRotationState(objRotation, objPosition, camera, mouseRay);
            drawRotationGizmo(objPosition, objRotation, camera);
            break;
    }
}

void Gizmo::updateState(Type type, Vector3& targetValue, const Vector3& gizmoPosition, const LitCamera& camera, const Ray& mouseRay) {
    if (m_dragContextType.has_value() && m_dragContextType.value() != type) {
        m_hoveredAxis = -1;
        return;
    }

    if (m_activeAxis != -1) {
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            processDrag(targetValue, mouseRay);
        }
    } else {
        m_hoveredAxis = -1;
        const float distanceToCamera = Vector3Distance(camera.position, gizmoPosition);

        if (type == Type::Position) {
            const float gizmoLength = std::max(distanceToCamera * LENGTH_SCALE, MIN_LENGTH);
            const float handleSize = std::max(distanceToCamera * HANDLE_SCALE, MIN_HANDLE_SIZE);
            const float coneHeight = 2.0f * std::max(distanceToCamera * HANDLE_SCALE, MIN_HANDLE_SIZE);

            for (int i = 0; i < AXIS_COUNT; ++i) {
                const Vector3 endPosition = Vector3Add(gizmoPosition, Vector3Scale(AXIS_DIRECTIONS[i * 2], gizmoLength));
                if (isPositionAxisHovered(mouseRay, gizmoPosition, endPosition, handleSize + coneHeight)) {
                    m_hoveredAxis = i * 2;
                    break;
                }
            }
        } else if (type == Type::Scale) {
            const float gizmoOffset = std::max(distanceToCamera * SCALE_GIZMO_OFFSET_SCALE, MIN_SCALE_GIZMO_OFFSET);
            const float handleSize = std::max(distanceToCamera * SCALE_HANDLE_SIZE_SCALE, MIN_SCALE_HANDLE_SIZE);
            for (int i = 0; i < AXIS_COUNT; ++i) {
                const Vector3 handlePosition = Vector3Add(gizmoPosition, Vector3Scale(AXIS_DIRECTIONS[i * 2], gizmoOffset));
                if (isScaleHandleHovered(mouseRay, handlePosition, handleSize)) {
                    m_hoveredAxis = i * 2;
                    break;
                }
            }
        }

        if (m_hoveredAxis != -1 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !m_dragContextType.has_value()) {
            beginDrag(type, mouseRay, targetValue, gizmoPosition, camera);
        }
    }

    gizmoActive = m_activeAxis != -1;
}

void Gizmo::updateRotationState(Quaternion& targetRotation, const Vector3& gizmoPosition, const LitCamera& camera, const Ray& mouseRay) {
    if (m_dragContextType.has_value() && m_dragContextType.value() != Type::Rotation) {
        m_hoveredAxis = -1;
        return;
    }

    if (m_activeAxis != -1) {
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            processRotationDrag(targetRotation, gizmoPosition, camera);
        }
    } else {
        m_hoveredAxis = -1;
        const float distanceToCamera = Vector3Distance(camera.position, gizmoPosition);
        const float radius = std::max(distanceToCamera * ROTATION_RADIUS_SCALE, MIN_ROTATION_RADIUS);
        const float trackballRadius = radius + 0.1 * distanceToCamera;
        const float ringThickness = radius * ROTATION_RING_THICKNESS;

        for (int i = 0; i < AXIS_COUNT + 1; ++i) {
            if (i == TRACKBALL_AXIS_INDEX && !trackBallEnabled) continue;

            Vector3 axis;
            if (i < AXIS_COUNT) {
                axis = ROTATION_AXES[i];
            } else {
                axis = Vector3Normalize(Vector3Subtract(camera.position, gizmoPosition));
            }

            if (isRotationHandleHovered(mouseRay, gizmoPosition, axis, (i == TRACKBALL_AXIS_INDEX) ? trackballRadius : radius, ringThickness)) {
                m_hoveredAxis = i;
                break;
            }
        }

        if (m_hoveredAxis != -1 && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !m_dragContextType.has_value()) {
            beginRotationDrag(mouseRay, targetRotation, gizmoPosition, camera);
        }
    }

    gizmoActive = m_activeAxis != -1;
}

void Gizmo::beginDrag(Type type, const Ray& mouseRay, const Vector3& currentTargetValue, const Vector3& gizmoPosition, const LitCamera& camera) {
    m_dragContextType = type;
    m_activeAxis = m_hoveredAxis;
    m_startDragValue = currentTargetValue;

    const Vector3 dragAxis = AXIS_DIRECTIONS[m_activeAxis];
    const Vector3 camToObject = Vector3Subtract(gizmoPosition, camera.position);
    Vector3 planeBasis = Vector3CrossProduct(camToObject, dragAxis);
    if (Vector3LengthSqr(planeBasis) < GIZMO_INTERSECTION_EPSILON * GIZMO_INTERSECTION_EPSILON) {
        Vector3 up = {0.0f, 1.0f, 0.0f};
        if (std::abs(Vector3DotProduct(dragAxis, up)) > PARALLEL_DOT_PRODUCT_THRESHOLD) up = {1.0f, 0.0f, 0.0f};
        planeBasis = Vector3CrossProduct(dragAxis, up);
    }
    m_dragPlaneNormal = Vector3Normalize(Vector3CrossProduct(planeBasis, dragAxis));
    const float rayPlaneDot = Vector3DotProduct(m_dragPlaneNormal, mouseRay.direction);
    if (std::abs(rayPlaneDot) > GIZMO_INTERSECTION_EPSILON) {
        const float t = Vector3DotProduct(Vector3Subtract(gizmoPosition, mouseRay.position), m_dragPlaneNormal) / rayPlaneDot;
        m_initialIntersectionPoint = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
    } else {
        m_initialIntersectionPoint = gizmoPosition;
    }
}

void Gizmo::beginRotationDrag(const Ray& mouseRay, const Quaternion& currentRotation, const Vector3& gizmoPosition, const LitCamera& camera) {
    m_dragContextType = Type::Rotation;
    m_activeAxis = m_hoveredAxis;
    m_startDragRotation = currentRotation;
    m_startDragMousePos = GetMousePosition();

    m_startDragEuler = GetContinuousEulerFromQuaternion(currentRotation, m_startDragEuler);

    if (m_activeAxis < AXIS_COUNT) {
        m_dragPlaneNormal = ROTATION_AXES[m_activeAxis];
    } else if (trackBallEnabled) {
        m_dragPlaneNormal = Vector3Normalize(Vector3Subtract(camera.position, gizmoPosition));
    }

    const float rayPlaneDot = Vector3DotProduct(m_dragPlaneNormal, mouseRay.direction);
    if (std::abs(rayPlaneDot) > GIZMO_INTERSECTION_EPSILON) {
        const float t = Vector3DotProduct(Vector3Subtract(gizmoPosition, mouseRay.position), m_dragPlaneNormal) / rayPlaneDot;
        m_initialIntersectionPoint = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
    } else {
        m_initialIntersectionPoint = gizmoPosition;
    }
}

void Gizmo::processDrag(Vector3& targetValue, const Ray& mouseRay) {
    const float rayPlaneDot = Vector3DotProduct(m_dragPlaneNormal, mouseRay.direction);
    if (std::abs(rayPlaneDot) <= GIZMO_INTERSECTION_EPSILON) return;

    const float t = Vector3DotProduct(Vector3Subtract(m_initialIntersectionPoint, mouseRay.position), m_dragPlaneNormal) / rayPlaneDot;
    const Vector3 currentIntersection = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));
    const Vector3 planeDisplacement = Vector3Subtract(currentIntersection, m_initialIntersectionPoint);
    const Vector3 dragAxis = AXIS_DIRECTIONS[m_activeAxis];
    const Vector3 projectedDisplacement = Vector3Scale(dragAxis, Vector3DotProduct(planeDisplacement, dragAxis));
    targetValue = Vector3Add(m_startDragValue, projectedDisplacement);

    if (gridSnappingEnabled) {
        if (m_dragContextType.has_value()) {
            if (m_dragContextType.value() == Type::Position) {
                targetValue.x = roundf(targetValue.x / gridSnappingFactor) * gridSnappingFactor;
                targetValue.y = roundf(targetValue.y / gridSnappingFactor) * gridSnappingFactor;
                targetValue.z = roundf(targetValue.z / gridSnappingFactor) * gridSnappingFactor;
            } else if (m_dragContextType.value() == Type::Scale) {
                Vector3 scaleFactor = Vector3Subtract(targetValue, m_startDragValue);
                scaleFactor.x = roundf(scaleFactor.x / gridSnappingFactor) * gridSnappingFactor;
                scaleFactor.y = roundf(scaleFactor.y / gridSnappingFactor) * gridSnappingFactor;
                scaleFactor.z = roundf(scaleFactor.z / gridSnappingFactor) * gridSnappingFactor;
                targetValue = Vector3Add(m_startDragValue, scaleFactor);
            }
        }
    }
}

void Gizmo::processRotationDrag(Quaternion& targetRotation, const Vector3& gizmoPosition, const LitCamera& camera) {
    if (m_activeAxis < AXIS_COUNT) {
        Ray mouseRay = GetMouseRay(GetMousePosition(), camera);
        const float rayPlaneDot = Vector3DotProduct(m_dragPlaneNormal, mouseRay.direction);
        if (std::abs(rayPlaneDot) <= GIZMO_INTERSECTION_EPSILON) return;

        const float t = Vector3DotProduct(Vector3Subtract(gizmoPosition, mouseRay.position), m_dragPlaneNormal) / rayPlaneDot;
        Vector3 currentIntersection = Vector3Add(mouseRay.position, Vector3Scale(mouseRay.direction, t));

        Vector3 startVec = Vector3Normalize(Vector3Subtract(m_initialIntersectionPoint, gizmoPosition));
        Vector3 currentVec = Vector3Normalize(Vector3Subtract(currentIntersection, gizmoPosition));

        float angleRad = Vector3Angle(startVec, currentVec);
        Vector3 cross = Vector3CrossProduct(startVec, currentVec);

        if (Vector3DotProduct(m_dragPlaneNormal, cross) < 0) {
            angleRad = -angleRad;
        }

        Vector3 euler = m_startDragEuler;
        float angleDeg = angleRad * RAD2DEG;

        if (m_activeAxis == 0)      euler.x += angleDeg;
        else if (m_activeAxis == 1) euler.y += angleDeg;
        else if (m_activeAxis == 2) euler.z += angleDeg;

        euler.x = NormalizeDegrees(euler.x);
        euler.z = NormalizeDegrees(euler.z);
        euler.y = std::clamp(euler.y, -GIMBAL_LOCK_PITCH_LIMIT, GIMBAL_LOCK_PITCH_LIMIT);

        targetRotation = QuaternionFromEuler(euler);
    } else if (trackBallEnabled) {
        Vector2 mouseDelta = Vector2Subtract(GetMousePosition(), m_startDragMousePos);
        m_startDragMousePos = GetMousePosition();

        constexpr float rotationSpeed = 0.004f;
        Quaternion rotX = QuaternionFromAxisAngle(camera.up, -mouseDelta.x * rotationSpeed);
        Quaternion rotY = QuaternionFromAxisAngle(camera.right, -mouseDelta.y * rotationSpeed);

        Quaternion untrustedRotation = QuaternionMultiply(rotX, targetRotation);
        untrustedRotation = QuaternionMultiply(rotY, untrustedRotation);

        Vector3 lastEuler = GetContinuousEulerFromQuaternion(targetRotation, m_startDragEuler);
        Vector3 newEuler = GetContinuousEulerFromQuaternion(untrustedRotation, lastEuler);

        newEuler.y = std::clamp(newEuler.y, -GIMBAL_LOCK_PITCH_LIMIT, GIMBAL_LOCK_PITCH_LIMIT);

        targetRotation = QuaternionFromEuler(newEuler);
        m_startDragEuler = newEuler;
    }
}

void Gizmo::endDrag() noexcept {
    m_dragContextType.reset();
    m_activeAxis = -1;
}

[[nodiscard]] bool Gizmo::isPositionAxisHovered(const Ray& mouseRay, const Vector3& start, const Vector3& end, float handleSize) const {
    constexpr float SHAFT_THICKNESS_SCALE = 0.5f;
    const float shaftThickness = handleSize * SHAFT_THICKNESS_SCALE;
    const float handleHalfSize = handleSize * SHAFT_THICKNESS_SCALE;
    const BoundingBox handleBBox = { Vector3SubtractValue(end, handleHalfSize), Vector3AddValue(end, handleHalfSize) };
    const BoundingBox shaftBBox = {
        { std::min(start.x, end.x) - shaftThickness, std::min(start.y, end.y) - shaftThickness, std::min(start.z, end.z) - shaftThickness },
        { std::max(start.x, end.x) + shaftThickness, std::max(start.y, end.y) + shaftThickness, std::max(start.z, end.z) + shaftThickness }
    };
    return GetRayCollisionBox(mouseRay, handleBBox).hit || GetRayCollisionBox(mouseRay, shaftBBox).hit;
}

[[nodiscard]] bool Gizmo::isScaleHandleHovered(const Ray& mouseRay, const Vector3& handlePosition, float handleSize) const {
    const float handleHalfSize = handleSize * 0.5f;
    const BoundingBox handleBBox = { Vector3SubtractValue(handlePosition, handleHalfSize), Vector3AddValue(handlePosition, handleHalfSize) };
    return GetRayCollisionBox(mouseRay, handleBBox).hit;
}

[[nodiscard]] bool Gizmo::isRotationHandleHovered(const Ray& mouseRay, const Vector3& center, const Vector3& axis, float radius, float thickness) const {
    const float innerRadius = radius - (thickness / 2.0f);
    const float outerRadius = radius + (thickness / 2.0f);

    Vector3 up = {0.0f, 1.0f, 0.0f};

    if (std::abs(Vector3DotProduct(axis, up)) > PARALLEL_DOT_PRODUCT_THRESHOLD) {
        up = {1.0f, 0.0f, 0.0f};
    }

    const Vector3 right = Vector3Normalize(Vector3CrossProduct(axis, up));
    const Vector3 localUp = Vector3Normalize(Vector3CrossProduct(right, axis));

    for (int i = 0; i < segments; ++i) {
        const float angle1 = ((float)i / segments) * 2.0f * PI;
        const float angle2 = ((float)(i + 1) / segments) * 2.0f * PI;

        const Vector3 dir1 = Vector3Add(Vector3Scale(right, cosf(angle1)), Vector3Scale(localUp, sinf(angle1)));
        const Vector3 dir2 = Vector3Add(Vector3Scale(right, cosf(angle2)), Vector3Scale(localUp, sinf(angle2)));

        const Vector3 p1 = Vector3Add(center, Vector3Scale(dir1, outerRadius));
        const Vector3 p2 = Vector3Add(center, Vector3Scale(dir1, innerRadius));
        const Vector3 p3 = Vector3Add(center, Vector3Scale(dir2, innerRadius));
        const Vector3 p4 = Vector3Add(center, Vector3Scale(dir2, outerRadius));

        if (GetRayCollisionQuad(mouseRay, p1, p2, p3, p4).hit) {
            return true;
        }
    }

    return false;
}

void Gizmo::drawPositionAxis(const Vector3& start, const Vector3& end, const Vector3& direction, float coneHeight, const Color& color) {
    DrawLine3D(start, end, color);

    const float startRadius = coneHeight * 0.5f;
    const Vector3 coneTip = Vector3Add(end, Vector3Scale(direction, coneHeight));

    DrawCylinderEx(end, coneTip, startRadius, 0.0f, 12, color);
}

void Gizmo::drawScaleHandle(const Vector3& position, float sideLength, const Color& color) {
    DrawCube(position, sideLength, sideLength, sideLength, color);
}

void Gizmo::drawPositionGizmo(const Vector3& position, const LitCamera& camera) const {
    const float distance = Vector3Distance(camera.position, position);
    const float length = std::max(distance * LENGTH_SCALE, MIN_LENGTH);
    const float coneHeight = 2.0f * std::max(distance * HANDLE_SCALE, MIN_HANDLE_SIZE);

    for (int index = 0; index < AXIS_COUNT; ++index) {
        const int direction = index * 2;
        const bool active = (m_dragContextType.value_or(Type::Position) == Type::Position) &&
                      (direction == m_activeAxis || direction == m_hoveredAxis);
        if (gizmoActive && !active) continue;

        const Vector3 end = Vector3Add(position, Vector3Scale(AXIS_DIRECTIONS[direction], length));
        drawPositionAxis(position, end, AXIS_DIRECTIONS[direction], coneHeight, active ? AXIS_HIGHLIGHT_COLORS[index] : AXIS_BASE_COLORS[index]);
    }
}

void Gizmo::drawScaleGizmo(const Vector3& position, const LitCamera& camera) const {
    const float distanceToCamera = Vector3Distance(camera.position, position);
    const float gizmoOffset = std::max(distanceToCamera * SCALE_GIZMO_OFFSET_SCALE, MIN_SCALE_GIZMO_OFFSET);
    const float handleSize  = std::max(distanceToCamera * SCALE_HANDLE_SIZE_SCALE, MIN_SCALE_HANDLE_SIZE);

    for (int index = 0; index < AXIS_COUNT; ++index) {
        const int direction = index * 2;
        const bool active = (m_dragContextType.value_or(Type::Scale) == Type::Scale) &&
                      (direction == m_activeAxis || direction == m_hoveredAxis);
        if (gizmoActive && !active) continue;

        const Color gizmoColor = active ? AXIS_HIGHLIGHT_COLORS[index] : AXIS_BASE_COLORS[index];
        const Vector3 scaleGizmoPosition = Vector3Add(position, Vector3Scale(AXIS_DIRECTIONS[direction], gizmoOffset));
        drawScaleHandle(scaleGizmoPosition, handleSize, gizmoColor);
    }
}

void Gizmo::drawRotationGizmo(const Vector3& position, const Quaternion& rotation, const LitCamera& camera) const {
    const float distanceToCamera = Vector3Distance(camera.position, position);
    const float radius = std::max(distanceToCamera * ROTATION_RADIUS_SCALE, MIN_ROTATION_RADIUS);
    const float trackballRadius = radius + 0.1 * distanceToCamera;
    const Vector3 defaultCircleNormal = {0.0f, 0.0f, 1.0f};

    for (int i = 0; i < AXIS_COUNT; ++i) {
        const bool active = (m_dragContextType.value_or(Type::Rotation) == Type::Rotation) && (i == m_activeAxis || i == m_hoveredAxis);
        if (gizmoActive && !active) continue;

        const Color ringColor = active ? AXIS_HIGHLIGHT_COLORS[i] : AXIS_BASE_COLORS[i];
        DrawCircle3D(position, radius, ROTATION_DIRECTIONS[i], 90, ringColor);
    }

    const int trackballAxis = AXIS_COUNT;
    const bool isTrackballActive = (m_dragContextType.value_or(Type::Rotation) == Type::Rotation) && (trackballAxis == m_activeAxis || trackballAxis == m_hoveredAxis);
    if ((!trackBallEnabled) || (gizmoActive && !isTrackballActive)) return;

    const Vector3 camDir = Vector3Normalize(Vector3Subtract(camera.position, position));
    Vector3 trackballRotationAxis = Vector3CrossProduct(defaultCircleNormal, camDir);
    float trackballRotationAngle = acosf(Vector3DotProduct(defaultCircleNormal, camDir)) * RAD2DEG;

    if (Vector3LengthSqr(trackballRotationAxis) < GIZMO_INTERSECTION_EPSILON) {
        trackballRotationAxis = (Vector3DotProduct(defaultCircleNormal, camDir) > 0.0f) ?
                                {0.0f, 0.0f, 1.0f} :
                                {1.0f, 0.0f, 0.0f};
        if (Vector3DotProduct(defaultCircleNormal, camDir) < 0.0f) trackballRotationAngle = 180.0f;
    }

    DrawCircle3D(position, trackballRadius, trackballRotationAxis, trackballRotationAngle, isTrackballActive ? YELLOW : WHITE);

    if (m_dragContextType.value_or(Type::Position) == Type::Rotation && m_activeAxis == trackballAxis) {
        DrawSphere(position, trackballRadius, {128, 128, 128, 50});
    }
}