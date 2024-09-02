void LoadGizmoModel(Gizmo& gizmo, const Model& model, const fs::path& modelPath, const Vector3& rotation = {0,0,0}) {
    if (modelPath.empty())
        gizmo.model = model;
    else
        gizmo.model = LoadModel(modelPath.string().c_str());

    if (!IsModelReady(gizmo.model)) {
        TraceLog(LOG_WARNING, "Gizmo model is not ready");
        return;
    }

    gizmo.rotation = rotation;
}

void InitGizmo() {
    for (int index = 0; index < NUM_GIZMO_ARROWS; ++index) {
        LoadGizmoModel(gizmoArrow[index], { 0 }, "assets/models/gizmo/arrow.obj", gizmoArrowOffsets[index].rotation);
    }

    LoadGizmoModel(gizmoTorus[0], LoadModelFromMesh(GenMeshSphere(1, 15, 15)), "");

    for (int index = 0; index < NUM_GIZMO_CUBES; ++index) {
        LoadGizmoModel(gizmoCube[index], LoadModelFromMesh(GenMeshCube(1, 1, 1)), "");
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

void UpdateGizmoProperties(float maxObjectScale = 0) {
    gizmoArrow[0].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxObjectScale + 6.0f, selectedObjectPosition.z };
    gizmoArrow[1].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxObjectScale - 6.0f, selectedObjectPosition.z };
    gizmoArrow[2].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxObjectScale + 6.0f };
    gizmoArrow[3].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxObjectScale - 6.0f };
    gizmoArrow[4].position = { selectedObjectPosition.x + maxObjectScale + 6.0f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoArrow[5].position = { selectedObjectPosition.x - maxObjectScale - 6.0f, selectedObjectPosition.y, selectedObjectPosition.z };

    gizmoCube[0].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxObjectScale + 2.0f, selectedObjectPosition.z };
    gizmoCube[1].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxObjectScale - 2.0f, selectedObjectPosition.z };
    gizmoCube[2].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxObjectScale + 2.0f };
    gizmoCube[3].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxObjectScale - 2.0f };
    gizmoCube[4].position = { selectedObjectPosition.x + maxObjectScale + 2.0f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[5].position = { selectedObjectPosition.x - maxObjectScale - 2.0f, selectedObjectPosition.y, selectedObjectPosition.z };
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

bool HandleGizmo(bool& draggingGizmoProperty, Vector3& selectedObjectProperty, int& selectedGizmoIndex) {
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

        switch (selectedGizmoIndex) {
            case 0: // Y-axis movement
            case 1:
                if (!gridSnappingEnabled) {
                    selectedObjectProperty.y += deltaY;
                } else if (fabs(accumulatedDeltaY) >= gridSnappingFactor) {
                    selectedObjectProperty.y -= round(accumulatedDeltaY / gridSnappingFactor) * gridSnappingFactor;
                    accumulatedDeltaY = 0.0f;
                }
                break;

            case 2: // Z-axis movement
            case 3:
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

            case 4: // X-axis movement
            case 5:
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
        }

        mouseDragStart = mouseDragEnd;
    }

    return true;
}


void DrawGizmo(Gizmo& gizmo, const bool& wireframe = false, const bool& applyRotation = false) {
    Matrix rotationMat = MatrixRotateXYZ((Vector3){
        DEG2RAD * (gizmo.rotation.x + (applyRotation ? selectedObjectRotation.x : 0)),
        DEG2RAD * (gizmo.rotation.y + (applyRotation ? selectedObjectRotation.y : 0)),
        DEG2RAD * (gizmo.rotation.z + (applyRotation ? selectedObjectRotation.z : 0))
    });

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
        for (int index = 0; index < NUM_GIZMO_ARROWS; ++index) {
            DrawGizmo(gizmoArrow[index]);
        }

        if (entitySelected) {
            for (int index = 0; index < NUM_GIZMO_CUBES; ++index) {
                DrawGizmo(gizmoCube[index]);
            }
        }

        DrawGizmo(gizmoTorus[0], true, true);
    }
}

void GizmoPosition() {
    if (!UpdateGizmoObjectProperties()) return;
    float maxObjectScale = 1.0f;
    if (selectedGameObjectType == "entity" && selectedEntity) maxObjectScale= std::max(std::max(std::abs(selectedEntity->scale.x), std::abs(selectedEntity->scale.y)), std::abs(selectedEntity->scale.z)) / 2 + 3;

    UpdateGizmoProperties(maxObjectScale);

    for (int index = 0; index < NUM_GIZMO_ARROWS; index++) {
        Color color = gizmoArrowColorsUnselected[index];
        IsGizmoBeingInteracted(gizmoArrow[index], index, selectedGizmoArrow);
        if (isHoveringGizmo) color = gizmoArrowColorsSelected[index];
        if (!HandleGizmo(draggingGizmoPosition, selectedObjectPosition, selectedGizmoArrow) && !isHoveringGizmo) 
            draggingGizmoPosition = false;   // Reset draggingGizmoPosition if not interacting

        gizmoArrow[index].color = color;
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

    for (int cubeIndex = 0; cubeIndex < NUM_GIZMO_CUBES; cubeIndex++) {
        Color color = { 0, 100, 0, 255 };

        IsGizmoBeingInteracted(gizmoCube[cubeIndex], cubeIndex, selectedGizmoCube);
        if (isHoveringGizmo) color = { 0, 150, 0, 255 };
        if (!HandleGizmo(draggingGizmoScale, selectedObjectScale, selectedGizmoCube) && !isHoveringGizmo) draggingGizmoScale = false;   // Reset draggingGizmoScale if the left mouse button isn't pressed or the gizmo isn't hovered
        gizmoCube[cubeIndex].color = color;
    }

    selectedEntity->scale = selectedObjectScale;
}

void GizmoRotation() {
    if (!UpdateGizmoObjectProperties()) return;

    float maxObjectScale = (selectedGameObjectType == "entity") ? 
                           std::max({std::abs(selectedEntity->scale.x), std::abs(selectedEntity->scale.y), std::abs(selectedEntity->scale.z)}) / 2 + 3 : 
                           3;
    Gizmo& torus = gizmoTorus[0];

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
            draggingGizmoRotation = true;
            dragging = true;
        }

        if (draggingGizmoRotation) {
            Vector2 mouseDragEnd = GetMousePosition();
            Vector3 rotationDelta = Vector3Transform(
                {-(mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor,
                 -(mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor,
                 0.0f},
                QuaternionToMatrix((Vector4){sceneCamera.front.x, sceneCamera.front.y, sceneCamera.front.z, 0})
            );

            selectedObjectRotation = {fmod(selectedObjectRotation.x + rotationDelta.x, 360.0f),
                                      fmod(selectedObjectRotation.y + rotationDelta.y, 360.0f),
                                      fmod(selectedObjectRotation.z + rotationDelta.z, 360.0f)};
            
            mouseDragStart = mouseDragEnd;
        }
    }
    else draggingGizmoRotation = false;

    if (selectedGameObjectType == "entity")      selectedEntity->rotation = selectedObjectRotation;
    else if (selectedGameObjectType == "light")  selectedLight->light.direction = {selectedObjectRotation.x / 360, selectedObjectRotation.y / 360, selectedObjectRotation.z / 360};
}