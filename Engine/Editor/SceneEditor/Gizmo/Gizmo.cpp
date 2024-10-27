#include "Gizmo.h"

void LoadGizmoModel(Gizmo& gizmo, const Model& model, const fs::path& modelPath, const int& gizmoAxis, const Vector3& rotation) {
    if (modelPath.empty()) gizmo.model = model;
    else gizmo.model = LoadModel(modelPath.string().c_str());

    if (!IsModelReady(gizmo.model)) {
        TraceLog(LOG_WARNING, "Gizmo model is not ready");
        return;
    }

    gizmo.rotation = rotation;
    gizmo.axis = static_cast<GizmoAxis>(gizmoAxis);
}

void InitGizmo() {
    for (int index = 0; index < NUM_GIZMO_POSITION; ++index) {
        LoadGizmoModel(gizmoPosition[index], { 0 }, "assets/models/gizmo/arrow.obj", (int)((index * 0.5) + 1), gizmoPositionOffsets[index].rotation);
    }

    LoadGizmoModel(gizmoRotation[0], LoadModelFromMesh(GenMeshSphere(1, 15, 15)), "", 0, { 0, 0, 0 });

    for (int index = 0; index < NUM_GIZMO_SCALE; ++index) {
        LoadGizmoModel(gizmoScale[index], LoadModelFromMesh(GenMeshCube(1, 1, 1)), "", (int)((index * 0.5) + 1), { 0, 0, 0 });
    }
}

bool UpdateGizmoObjectProperties() {
    if (selectedGameObjectType == "entity" && selectedEntity) {
        selectedObjectPosition = selectedEntity->position;
        selectedObjectScale = selectedEntity->scale;
        selectedObjectRotation = selectedEntity->rotation;
    } else if (selectedGameObjectType == "light" && selectedLight) {
        selectedObjectPosition = glm3ToVec3(selectedLight->light.position);
        selectedObjectScale = {1, 1, 1};
        selectedObjectRotation = Vector3Multiply(glm3ToVec3(selectedLight->light.direction), Vector3{ 360, 360, 360 });
    } else return false; // Failure

    return true; // Success
}

void UpdateGizmoProperties(const float& maxObjectScale = 0) {
    constexpr float gizmoPositionOffset = 6.0f;
    constexpr float gizmoScaleOffset = 2.0f;

    gizmoPosition[0].position = { selectedObjectPosition.x + maxObjectScale + gizmoPositionOffset, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoPosition[1].position = { selectedObjectPosition.x - maxObjectScale - gizmoPositionOffset, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoPosition[2].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxObjectScale + gizmoPositionOffset, selectedObjectPosition.z };
    gizmoPosition[3].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxObjectScale - gizmoPositionOffset, selectedObjectPosition.z };
    gizmoPosition[4].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxObjectScale + gizmoPositionOffset };
    gizmoPosition[5].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxObjectScale - gizmoPositionOffset };

    gizmoScale[0].position = { selectedObjectPosition.x + maxObjectScale + gizmoScaleOffset, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoScale[1].position = { selectedObjectPosition.x - maxObjectScale - gizmoScaleOffset, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoScale[2].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxObjectScale + gizmoScaleOffset, selectedObjectPosition.z };
    gizmoScale[3].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxObjectScale - gizmoScaleOffset, selectedObjectPosition.z };
    gizmoScale[4].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxObjectScale + gizmoScaleOffset };
    gizmoScale[5].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxObjectScale - gizmoScaleOffset };
}

void IsGizmoBeingInteracted(const Gizmo& gizmo, const int& index, int& selectedGizmoIndex) {
    const ImGuiPayload* payload = ImGui::GetDragDropPayload(); // Check if any payload is being dragged
    if (dragging || payload || !ImGui::IsWindowHovered()) {
        isHoveringGizmo = false;
        return;
    }

    isHoveringGizmo = IsMouseHoveringModel(gizmo.model, gizmo.position, gizmo.rotation, gizmo.scale);

    if (isHoveringGizmo) selectedGizmoIndex = index;
}

static float accumulatedDeltaX = 0.0f;
static float accumulatedDeltaY = 0.0f;

void SetGizmoVisibility(Gizmo& gizmo, const bool& isVisible) {
    if (isVisible) {
        gizmo.color.a = 255;
    } else {
        gizmo.color.a = 100;
    }
}

void UpdateGizmoVisibilityAndScaling(Gizmo& gizmo, const Camera& camera) {
    Vector3 camToGizmo = Vector3Subtract(gizmo.position, camera.position);
    float distance = Vector3Length(camToGizmo);

    gizmo.scale = { distance * 0.05f, distance * 0.05f, distance * 0.05f };

    bool isVisible = (distance < 50.0f || isHoveringGizmo);
    SetGizmoVisibility(gizmo, isVisible);
}

bool HandleGizmo(bool& draggingGizmoProperty, Vector3& selectedObjectProperty, const GizmoAxis& axis, const bool& applyMinimumConstraint = false) {
    if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        accumulatedDeltaX = accumulatedDeltaY = 0.0f;
        draggingGizmoProperty = false;
        dragging = false;
        return false;
    }

    if (isHoveringGizmo && !dragging) {
        mouseDragStart = GetMousePosition();
        draggingGizmoProperty = true;
        dragging = true;
    }

    if (draggingGizmoProperty) {
        Vector2 mouseDragEnd = GetMousePosition();
        float deltaX = (mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
        float deltaY = (mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;

        accumulatedDeltaX += deltaX;
        accumulatedDeltaY += deltaY;

        Vector3 cameraDirection = Vector3Normalize(Vector3Subtract(sceneCamera.position, selectedObjectProperty));

        switch (axis) {
            case GizmoAxis::X_AXIS:
                if (cameraDirection.z > 0) {
                    accumulatedDeltaX = -accumulatedDeltaX;
                    deltaX = -deltaX;
                }
                if (!gridSnappingEnabled) {
                    selectedObjectProperty.x += deltaX;
                } else if (fabs(accumulatedDeltaX) >= gridSnappingFactor) {
                    selectedObjectProperty.x -= round(accumulatedDeltaX / gridSnappingFactor) * gridSnappingFactor;
                    accumulatedDeltaX = 0.0f;
                }
                break;

            case GizmoAxis::Y_AXIS:
                if (!gridSnappingEnabled) {
                    selectedObjectProperty.y += deltaY;
                } else if (fabs(accumulatedDeltaY) >= gridSnappingFactor) {
                    selectedObjectProperty.y -= round(accumulatedDeltaY / gridSnappingFactor) * gridSnappingFactor;
                    accumulatedDeltaY = 0.0f;
                }
                break;

            case GizmoAxis::Z_AXIS:
                if (cameraDirection.x > 0) {
                    accumulatedDeltaX = -accumulatedDeltaX;
                    deltaX = -deltaX;
                }
                if (!gridSnappingEnabled) {
                    selectedObjectProperty.z += (deltaX + deltaY);
                } else if (fabs(accumulatedDeltaX + accumulatedDeltaY) >= gridSnappingFactor) {
                    selectedObjectProperty.z += round((accumulatedDeltaX + accumulatedDeltaY) / gridSnappingFactor) * gridSnappingFactor;
                    accumulatedDeltaX = accumulatedDeltaY = 0.0f;
                }
                break;
        }

        if (applyMinimumConstraint) {
            if (selectedObjectProperty.x < 0.0f) selectedObjectProperty.x = 0.0f;
            if (selectedObjectProperty.y < 0.0f) selectedObjectProperty.y = 0.0f;
            if (selectedObjectProperty.z < 0.0f) selectedObjectProperty.z = 0.0f;
        }

        mouseDragStart = mouseDragEnd;
    }

    return true;
}


void DrawGizmo(Gizmo& gizmo, const bool& wireframe = false, const bool& applyRotation = false) {
    if (dragging) {
        Ray ray;
        Color rayColor;

        ray.position = gizmo.position;

        if (gizmo.axis == GizmoAxis::X_AXIS) {
            rayColor = RED;
            ray.direction = Vector3{1,0,0};
        } else if (gizmo.axis == GizmoAxis::Y_AXIS) {
            rayColor = BLUE;
            ray.direction = Vector3{0,1,0};
        } else if (gizmo.axis == GizmoAxis::Z_AXIS) {
            rayColor = GREEN;
            ray.direction = Vector3{0,0,1};
        }

        DrawRay(ray, rayColor);
    }

    Matrix rotationMat = MatrixRotateXYZ((Vector3){
        DEG2RAD * (gizmo.rotation.x + (applyRotation ? selectedObjectRotation.x : 0)),
        DEG2RAD * (gizmo.rotation.y + (applyRotation ? selectedObjectRotation.y : 0)),
        DEG2RAD * (gizmo.rotation.z + (applyRotation ? selectedObjectRotation.z : 0))
    });

    UpdateGizmoVisibilityAndScaling(gizmo, sceneCamera);

    Matrix transformMatrix = MatrixIdentity();
    transformMatrix = MatrixMultiply(transformMatrix, MatrixScale(gizmo.scale.x, gizmo.scale.y, gizmo.scale.z));
    transformMatrix = MatrixMultiply(transformMatrix, rotationMat);
    transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(gizmo.position.x, gizmo.position.y, gizmo.position.z));

    gizmo.model.transform = transformMatrix;

    if (wireframe)
        DrawModelWires(gizmo.model, Vector3Zero(), 1, gizmo.color);
    else
        DrawModel(gizmo.model, Vector3Zero(), 1, gizmo.color);
}

void DrawGizmos() {
    bool entitySelected = (selectedGameObjectType == "entity" && selectedEntity);
    bool lightSelected = (selectedGameObjectType == "light" && selectedLight);
    if (entitySelected || lightSelected) {
        if (!dragging || draggingPositionGizmo) {
            for (int index = 0; index < NUM_GIZMO_POSITION; ++index) {
                if (index == selectedPositionGizmo || !draggingPositionGizmo) DrawGizmo(gizmoPosition[index]);
            }
        }

        if (entitySelected) {
            if (!dragging || draggingScaleGizmo) {
                for (int index = 0; index < NUM_GIZMO_POSITION; ++index) {
                    if (index == selectedScaleGizmo || !draggingScaleGizmo) DrawGizmo(gizmoScale[index]);
                }
            }
        }

        if (!dragging || draggingRotationGizmo) DrawGizmo(gizmoRotation[0], true, true);
    }
}

void GizmoPosition() {
    if (!UpdateGizmoObjectProperties()) return;
    float maxObjectScale = 1.0f;
    if (selectedGameObjectType == "entity" && selectedEntity) maxObjectScale= std::max(std::max(std::abs(selectedEntity->scale.x), std::abs(selectedEntity->scale.y)), std::abs(selectedEntity->scale.z)) / 2 + 3;

    UpdateGizmoProperties(maxObjectScale);

    for (int index = 0; index < NUM_GIZMO_POSITION; index++) {
        Color color = gizmoPositionColorsUnselected[index];
        IsGizmoBeingInteracted(gizmoPosition[index], index, selectedPositionGizmo);
        if (isHoveringGizmo) color = gizmoPositionColorsSelected[index];

        if (!draggingPositionGizmo || (draggingPositionGizmo && selectedPositionGizmo == index)) {
            if (!HandleGizmo(draggingPositionGizmo, selectedObjectPosition, gizmoPosition[index].axis) && !isHoveringGizmo)
                draggingPositionGizmo = false;   // Reset draggingPositionGizmo if not interacting
        }

        gizmoPosition[index].color = color;
    }

    if (selectedGameObjectType == "entity" && selectedEntity != nullptr) {
        selectedEntity->position = selectedObjectPosition;
        if (selectedEntity->isChild && selectedEntity->parent && selectedEntity->initialized) {
            selectedEntity->relativePosition = Vector3Subtract(selectedEntity->position, selectedEntity->parent->position);
        }
    } else if (selectedGameObjectType == "light" && selectedLight) {
        selectedLight->light.position = vec3ToGlm3(selectedObjectPosition);
        if (selectedLight->isChild) {
            selectedLight->light.relativePosition = glm::vec3(
                selectedLight->light.position.x - selectedLight->parent->position.x,
                selectedLight->light.position.y - selectedLight->parent->position.y,
                selectedLight->light.position.z - selectedLight->parent->position.z
            );
        }
    }
}

void GizmoScale() {
    if (!UpdateGizmoObjectProperties()) return;

    float maxObjectScale;
    selectedObjectPosition = selectedEntity->position;
    maxObjectScale = std::max(std::max(std::abs(selectedEntity->scale.x), std::abs(selectedEntity->scale.y)), std::abs(selectedEntity->scale.z)) / 2 + 3;
    selectedObjectScale = selectedEntity->scale;

    UpdateGizmoProperties(maxObjectScale);

    for (int cubeIndex = 0; cubeIndex < NUM_GIZMO_SCALE; cubeIndex++) {
        Color color = { 0, 100, 0, 255 };

        IsGizmoBeingInteracted(gizmoScale[cubeIndex], cubeIndex, selectedScaleGizmo);
        if (isHoveringGizmo) color = { 0, 150, 0, 255 };
        if (!draggingScaleGizmo || (draggingScaleGizmo && selectedScaleGizmo == cubeIndex)) {
            if (!HandleGizmo(draggingScaleGizmo, selectedObjectScale, gizmoScale[cubeIndex].axis, true) && !isHoveringGizmo) draggingScaleGizmo = false;   // Reset draggingScaleGizmo if the left mouse button isn't pressed or the gizmo isn't hovered
        }
        gizmoScale[cubeIndex].color = color;
    }

    selectedEntity->scale = selectedObjectScale;
}

void ApplyRotationConstraints(Vector3& rotationDelta, const Vector3& gizmoRotationDeltaPrevious, const bool& lockX, const bool& lockY, const bool& lockZ) {
    if (lockX) rotationDelta.x = gizmoRotationDeltaPrevious.x;
    if (lockY) rotationDelta.y = gizmoRotationDeltaPrevious.y;
    if (lockZ) rotationDelta.z = gizmoRotationDeltaPrevious.z;
}

void GizmoRotation() {
    if (!UpdateGizmoObjectProperties()) return;

    float maxObjectScale = (selectedGameObjectType == "entity") ?
                           std::max({std::abs(selectedEntity->scale.x), std::abs(selectedEntity->scale.y), std::abs(selectedEntity->scale.z)}) / 2 + 3 : 
                           3;
    Gizmo& torus = gizmoRotation[0];

    torus.scale = { maxObjectScale, maxObjectScale, maxObjectScale };
    torus.position = selectedObjectPosition;

    isHoveringGizmo = IsMouseHoveringModel(torus.model, torus.position, torus.rotation, torus.scale);

    Color color = !dragging && ImGui::IsWindowHovered
        ? isHoveringGizmo
            ? Color{150, 150, 150, 255} : Color{100, 100, 100, 120}
        : Color{100, 0, 0, 120};

    torus.color = color;

    if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        if (isHoveringGizmo && !dragging) {
            mouseDragStart = GetMousePosition();
            draggingRotationGizmo = true;
            dragging = true;
        }

        if (draggingRotationGizmo) {
            Vector2 mouseDragEnd = GetMousePosition();
            Vector3 gizmoRotationDeltaPrevious = gizmoRotationDelta;

            gizmoRotationDelta = Vector3Transform(
                {-(mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor,
                 -(mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor,
                 0.0f},
                QuaternionToMatrix((Vector4){sceneCamera.front.x, sceneCamera.front.y, sceneCamera.front.z, 0})
            );

            if (IsKeyPressed(KEY_X)) lockRotationX = !lockRotationX;
            if (IsKeyPressed(KEY_Y)) lockRotationY = !lockRotationY;
            if (IsKeyPressed(KEY_Z)) lockRotationZ = !lockRotationZ;

            ApplyRotationConstraints(gizmoRotationDelta, gizmoRotationDeltaPrevious, lockRotationX, lockRotationY, lockRotationZ);

            selectedObjectRotation = {fmod(selectedObjectRotation.x + gizmoRotationDelta.x, 360.0f),
                                      fmod(selectedObjectRotation.y + gizmoRotationDelta.y, 360.0f),
                                      fmod(selectedObjectRotation.z + gizmoRotationDelta.z, 360.0f)};

            mouseDragStart = mouseDragEnd;
        }
    } else draggingRotationGizmo = false;

    if (selectedGameObjectType == "entity")      selectedEntity->rotation = selectedObjectRotation;
    else if (selectedGameObjectType == "light")  selectedLight->light.direction = {selectedObjectRotation.x / 360, selectedObjectRotation.y / 360, selectedObjectRotation.z / 360};
}