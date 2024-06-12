#include "Gizmo.h"
#include "../../../../include_all.h"

void LoadGizmoModel(Gizmo& gizmo, const char* modelPath, const Shader& shader, const Vector3& rotation = {0,0,0}) {
    gizmo.model = LoadModel(modelPath);
    gizmo.rotation = rotation;
    gizmo.model.materials[0].shader = shader;
}

void InitGizmo() {
    for (int index = 0; index < NUM_GIZMO_ARROWS; ++index) {
        LoadGizmoModel(gizmoArrow[index], "assets/models/gizmo/arrow.obj", shader, gizmoArrowOffsets[index].rotation);
    }

    for (int index = 0; index < NUM_GIZMO_TAURUS; ++index) {
        gizmoTaurus[0].model = LoadModelFromMesh(GenMeshSphere(1, 15, 15));
        gizmoTaurus[0].model.materials[0].shader = shader;
    }
    gizmoTaurus[0].rotation = {0, 0, 0};

    for (int index = 0; index < NUM_GIZMO_CUBES; ++index) {
        gizmoCube[index].model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
        gizmoCube[index].model.materials[0].shader = shader;
    }
}

int UpdateGizmoObjectProperties(std::string type) {
    if (type == "entity" && selectedEntity) {
        selectedObjectPosition = selectedEntity->position;
        selectedObjectScale = selectedEntity->scale;
        selectedObjectRotation = selectedEntity->rotation;
    } else if (type == "light" && selectedLight) {
        selectedObjectPosition = glm3ToVec3(selectedLight->position);
        selectedObjectScale = {1, 1, 1};
        selectedObjectRotation = Vector3Multiply(glm3ToVec3(selectedLight->direction), Vector3{ 360, 360, 360 });
    } else return 1; // Failure

    return 0; // Success
}

void UpdateGizmoProperties(float maxObjectScale = 0) {
    gizmoArrow[0].position = { selectedObjectPosition.x, selectedObjectPosition.y + selectedObjectScale.y, selectedObjectPosition.z };
    gizmoArrow[1].position = { selectedObjectPosition.x, selectedObjectPosition.y - selectedObjectScale.y, selectedObjectPosition.z };
    gizmoArrow[2].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + selectedObjectScale.z };
    gizmoArrow[3].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - selectedObjectScale.z };
    gizmoArrow[4].position = { selectedObjectPosition.x + selectedObjectScale.x, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoArrow[5].position = { selectedObjectPosition.x - selectedObjectScale.x, selectedObjectPosition.y, selectedObjectPosition.z };

    for (int index = 0; index < 6; ++index) {
        gizmoArrow[index].position = Vector3Add(gizmoArrow[index].position, gizmoArrowOffsets[index].position);
    }

    gizmoCube[0].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxObjectScale + 3.75f, selectedObjectPosition.z };
    gizmoCube[1].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxObjectScale - 3.75f, selectedObjectPosition.z };
    gizmoCube[2].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxObjectScale + 3.75f };
    gizmoCube[3].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxObjectScale - 3.75f };
    gizmoCube[4].position = { selectedObjectPosition.x + maxObjectScale + 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[5].position = { selectedObjectPosition.x - maxObjectScale - 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
}

void IsGizmoBeingInteracted(Gizmo& gizmo, int index, Color& color, int& selectedGizmoIndex) {
    if (dragging && !ImGui::IsWindowHovered()) return;
    isHoveringGizmo = IsMouseHoveringModel(gizmo.model, sceneCamera, gizmo.position, gizmo.rotation, gizmo.scale, nullptr, true);
        
    if (isHoveringGizmo)
    {
        color = GREEN;
        selectedGizmoIndex = index;
    }
}

bool HandleGizmo(bool& draggingGizmoProperty, Vector3& selectedObjectProperty, int& selectedGizmoIndex) {
    if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) return false; // Failure

    if (isHoveringGizmo && !dragging) {
        mouseDragStart = GetMousePosition();
        draggingGizmoProperty = true;
    }

    if (draggingGizmoProperty)
    {
        Vector2 mouseDragEnd = GetMousePosition();
        float deltaX = (mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
        float deltaY = (mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;

        switch (selectedGizmoIndex) {
            case 0:
            case 1:
                selectedObjectProperty.y -= deltaY;
                break;

            case 2:
            case 3:
                selectedObjectProperty.z -= deltaX + deltaY;
                break;

            case 4:
            case 5:
                selectedObjectProperty.x += deltaX;
                break;
        }

        mouseDragStart = mouseDragEnd;
    }

    return true; // Success
}

void DrawGizmo(Gizmo& gizmo, Color color, bool wireframe = false, bool applyRotation = false) {
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
        DrawModelWires(gizmo.model, Vector3Zero(), 1, color);
    else
        DrawModel(gizmo.model, Vector3Zero(), 1, color);
}

void GizmoPosition()
{
    if (UpdateGizmoObjectProperties(selectedGameObjectType)) return;
    UpdateGizmoProperties();

    for (int index = 0; index < NUM_GIZMO_ARROWS; index++)
    {
        Color color1 = RED;
        IsGizmoBeingInteracted(gizmoArrow[index], index, color1, selectedGizmoArrow);
        if (!HandleGizmo(draggingGizmoPosition, selectedObjectPosition, selectedGizmoArrow) && !isHoveringGizmo) draggingGizmoPosition = false;   // Reset draggingGizmoPosition if the left mouse button isn't pressed or the gizmo isn't hovered
        DrawGizmo(gizmoArrow[index], color1);
    }

    if (selectedGameObjectType == "entity")
    {
        selectedEntity->position = selectedObjectPosition;        
        if (selectedEntity->isChild)
        {
            if (selectedEntity->parent != nullptr && selectedEntity != nullptr && selectedEntity->initialized)
                selectedEntity->relativePosition = Vector3Subtract(selectedEntity->position, selectedEntity->parent->position);
        }
    }
    else if (selectedGameObjectType == "light")
    {
        selectedLight->position.x = selectedObjectPosition.x;
        selectedLight->position.y = selectedObjectPosition.y;
        selectedLight->position.z = selectedObjectPosition.z;

        if (selectedLight->isChild)
        {
            auto it = std::find_if(lightsInfo.begin(), lightsInfo.end(), [&](const AdditionalLightInfo& light) {
                return light.id == selectedLight->id;
            });

            AdditionalLightInfo* lightInfo = (AdditionalLightInfo*)&*it;

            if (it != lightsInfo.end()) {
                if (lightInfo->parent != nullptr && selectedLight != nullptr && lightInfo != nullptr)
                    selectedLight->relativePosition = glm::vec3(
                        selectedLight->position.x - lightInfo->parent->position.x, 
                        selectedLight->position.y - lightInfo->parent->position.y,
                        selectedLight->position.z - lightInfo->parent->position.z
                    );
            }
        
        }
    }
}


void GizmoScale()
{
    if (selectedGameObjectType != "entity") return;

    float maxObjectScale;
    selectedObjectPosition = selectedEntity->position;
    maxObjectScale = std::max(std::max(selectedEntity->scale.x, selectedEntity->scale.y), selectedEntity->scale.z) / 2 + 3;
    selectedObjectScale = selectedEntity->scale;

    UpdateGizmoProperties(maxObjectScale);

    for (int cubeIndex = 0; cubeIndex < NUM_GIZMO_CUBES; cubeIndex++)
    {
        Color color1 = { 40, 180, 40, 250 };

        IsGizmoBeingInteracted(gizmoCube[cubeIndex], cubeIndex, color1, selectedGizmoCube);
        if (!HandleGizmo(draggingGizmoScale, selectedObjectScale, selectedGizmoCube) && !isHoveringGizmo) draggingGizmoScale = false;   // Reset draggingGizmoScale if the left mouse button isn't pressed or the gizmo isn't hovered
        DrawGizmo(gizmoCube[cubeIndex], color1);
    }

    selectedEntity->scale = selectedObjectScale;
}


void GizmoRotation()
{
    UpdateGizmoObjectProperties(selectedGameObjectType);

    float maxObjectScale = (selectedGameObjectType == "entity") ? 
                           std::max({selectedEntity->scale.x, selectedEntity->scale.y, selectedEntity->scale.z}) / 2 + 3 : 
                           3;

    gizmoTaurus[0].scale = { maxObjectScale, maxObjectScale, maxObjectScale };
    gizmoTaurus[0].position = selectedObjectPosition;

    Color color1 = (!draggingGizmoScale || !draggingGizmoRotation || !draggingGizmoPosition) && ImGui::IsWindowHovered() ?
                  (IsMouseHoveringModel(gizmoTaurus[0].model, sceneCamera, gizmoTaurus[0].position, gizmoTaurus[0].rotation, gizmoTaurus[0].scale, nullptr, true) ? 
                   (Color){150, 150, 150, 255} : (Color){100, 100, 100, 120}) : (Color){100, 0, 0, 120};

    isHoveringGizmo = IsMouseHoveringModel(gizmoTaurus[0].model, sceneCamera, gizmoTaurus[0].position, gizmoTaurus[0].rotation, gizmoTaurus[0].scale, nullptr, true);
        
    if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
        if (isHoveringGizmo && !dragging) {
            mouseDragStart = GetMousePosition();
            draggingGizmoRotation = true;
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

    DrawGizmo(gizmoTaurus[0], color1, true, true);

    if (selectedGameObjectType == "entity")      selectedEntity->rotation = selectedObjectRotation;
    else if (selectedGameObjectType == "light")  selectedLight->direction = {selectedObjectRotation.x / 300, selectedObjectRotation.y / 300, selectedObjectRotation.z / 300};
}