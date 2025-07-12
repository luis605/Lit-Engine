/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef GIZMO_HPP
#define GIZMO_HPP

#include <raylib.h>
#include <array>
#include <cstdint>
#include <optional>

struct LitCamera;

class Gizmo {
public:
    enum class Type : std::uint8_t {
        Position,
        Scale,
        Rotation
    };

    Gizmo() noexcept = default;

    void beginFrame();
    void endFrame();
    void updateAndDraw(Type type, Vector3& objPosition, Vector3& objScale, Quaternion& objRotation, const LitCamera& camera, const Rectangle& viewport);
    inline bool isGizmoActive() const noexcept { return gizmoActive; };

private:
    int m_activeAxis{-1};
    int m_hoveredAxis{-1};

    bool gizmoActive = false;
    const bool trackBallEnabled = false;

    std::optional<Type> m_dragContextType;

    static constexpr float PARALLEL_DOT_PRODUCT_THRESHOLD = 0.99f;
    static constexpr float MINIMUM_LEGAL_SCALE = 0.001f;
    static constexpr float GIMBAL_LOCK_PITCH_LIMIT = 89.9f;
    static constexpr int segments = 32;
    static constexpr int TRACKBALL_AXIS_INDEX = 3;

    Vector3 m_startDragEuler;
    Vector3 m_startDragValue{};
    Quaternion m_startDragRotation{};
    Vector3 m_initialIntersectionPoint{};
    Vector3 m_dragPlaneNormal{};
    Vector2 m_startDragMousePos{};

    void updateState(Type type, Vector3& targetValue, const Vector3& gizmoPosition, const LitCamera& camera, const Ray& mouseRay);
    void beginDrag(Type type, const Ray& mouseRay, const Vector3& currentTargetValue, const Vector3& gizmoPosition, const LitCamera& camera);
    void processDrag(Vector3& targetValue, const Ray& mouseRay);

    void updateRotationState(Quaternion& targetRotation, const Vector3& gizmoPosition, const LitCamera& camera, const Ray& mouseRay);
    void beginRotationDrag(const Ray& mouseRay, const Quaternion& currentRotation, const Vector3& gizmoPosition, const LitCamera& camera);
    void processRotationDrag(Quaternion& targetRotation, const Vector3& gizmoPosition, const LitCamera& camera);

    void endDrag() noexcept;

    void drawPositionGizmo(const Vector3& position, const LitCamera& camera) const;
    void drawScaleGizmo(const Vector3& position, const LitCamera& camera) const;
    void drawRotationGizmo(const Vector3& position, const Quaternion& rotation, const LitCamera& camera) const;

    [[nodiscard]] bool isPositionAxisHovered(const Ray& mouseRay, const Vector3& start, const Vector3& end, float handleSize) const;
    [[nodiscard]] bool isScaleHandleHovered(const Ray& mouseRay, const Vector3& handlePosition, float handleSize) const;
    [[nodiscard]] bool isRotationHandleHovered(const Ray& mouseRay, const Vector3& center, const Vector3& axis, float radius, float thickness) const;

    static void drawPositionAxis(const Vector3& start, const Vector3& end, const Vector3& direction, float handleSize, const Color& color);
    static void drawScaleHandle(const Vector3& position, float sideLength, const Color& color);

    static constexpr int AXIS_COUNT = 3;
    static constexpr int AXIS_DIRECTIONS_COUNT = AXIS_COUNT * 2;
    static constexpr int ROTATION_AXIS_COUNT = 4;
    static constexpr float LENGTH_SCALE = 0.20f;
    static constexpr float HANDLE_SCALE = 0.02f;
    static constexpr float MIN_LENGTH = 3.5f;
    static constexpr float MIN_HANDLE_SIZE = 0.1f;
    static constexpr float SCALE_GIZMO_OFFSET_SCALE = 0.30f;
    static constexpr float MIN_SCALE_GIZMO_OFFSET = 4.5f;
    static constexpr float SCALE_HANDLE_SIZE_SCALE = 0.025f;
    static constexpr float MIN_SCALE_HANDLE_SIZE = 0.15f;
    static constexpr float ROTATION_RADIUS_SCALE = 0.265f;
    static constexpr float MIN_ROTATION_RADIUS = 4.1f;
    static constexpr float ROTATION_RING_THICKNESS = 0.1f;
    static constexpr float AXIS_LINE_WIDTH = 5.0f;
    static constexpr float DEFAULT_LINE_WIDTH = 1.0f;
    static constexpr float GIZMO_INTERSECTION_EPSILON = 1e-6f;

    static const std::array<Vector3, AXIS_DIRECTIONS_COUNT> AXIS_DIRECTIONS;
    static const std::array<Color, AXIS_COUNT> AXIS_BASE_COLORS;
    static const std::array<Color, AXIS_COUNT> AXIS_HIGHLIGHT_COLORS;
    static const std::array<Vector3, AXIS_COUNT> ROTATION_AXES;
    static const std::array<Vector3, AXIS_COUNT> ROTATION_DIRECTIONS;
};

extern Gizmo editorGizmo;

#endif // GIZMO_HPP