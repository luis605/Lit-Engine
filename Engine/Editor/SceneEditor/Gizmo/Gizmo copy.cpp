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
        gizmoTaurus[index].model = LoadModelFromMesh(GenMeshSphere(1, 30, 30));
        gizmoTaurus[index].model.materials[0].shader = shader;
    }
    gizmoTaurus[0].rotation = {0, 0, 0};

    for (int index = 0; index < NUM_GIZMO_CUBES; ++index) {
        gizmoCube[index].model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
        gizmoCube[index].model.materials[0].shader = shader;
    }
}

int UpdateGizmoObjectProperties(const char* type) {
    if (strcmp(type, "entity") == 0 && selectedEntity != nullptr) {
        selectedObjectPosition = selectedEntity->position;
        selectedObjectScale = selectedEntity->scale;
    } else if (strcmp(type, "light") == 0 && selectedLight != nullptr) {
        selectedObjectPosition = glm3ToVec3(selectedLight->position);
        selectedObjectScale = {1, 1, 1};
    } else {
        return 1; // Failure
    }

    return 0; // Success
}

void UpdateGizmoProperties(float maxScale = 0) {
    gizmoArrow[0].position = { selectedObjectPosition.x, selectedObjectPosition.y + selectedObjectScale.y, selectedObjectPosition.z };
    gizmoArrow[1].position = { selectedObjectPosition.x, selectedObjectPosition.y - selectedObjectScale.y, selectedObjectPosition.z };
    gizmoArrow[2].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + selectedObjectScale.z };
    gizmoArrow[3].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - selectedObjectScale.z };
    gizmoArrow[4].position = { selectedObjectPosition.x + selectedObjectScale.x, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoArrow[5].position = { selectedObjectPosition.x - selectedObjectScale.x, selectedObjectPosition.y, selectedObjectPosition.z };

    for (int index = 0; index < 6; ++index) {
        gizmoArrow[index].position = Vector3Add(gizmoArrow[index].position, gizmoArrowOffsets[index].position);
    }

    gizmoCube[0].position = { selectedObjectPosition.x + maxScale + 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[1].position = { selectedObjectPosition.x - maxScale - 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[2].position = { selectedObjectPosition.x, selectedObjectPosition.y + maxScale + 3.75f, selectedObjectPosition.z };
    gizmoCube[3].position = { selectedObjectPosition.x, selectedObjectPosition.y - maxScale - 3.75f, selectedObjectPosition.z };
    gizmoCube[4].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + maxScale + 3.75f };
    gizmoCube[5].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - maxScale - 3.75f };
}

bool IsGizmoBeingInteracted(Gizmo gizmo, int index, Color& color, int& selectedGizmoIndex) {
    if (dragging && !ImGui::IsWindowHovered())
        return false;
    
    bool hovered = IsMouseHoveringModel(gizmo.model, sceneCamera, gizmo.position, gizmo.rotation, gizmo.scale, nullptr, true);
    if (hovered) {
        color = GREEN;
        selectedGizmoIndex = index;
    } else {
        color = RED;
        selectedGizmoIndex = -1;
    }
}

int HandleGizmoPosition() {
    if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON)) return 1; // Failure

    std::cout << "hiuh uiohiuh uih " << std::endl;

    if (isHoveringGizmo && !draggingGizmoPosition) {
        mouseDragStart = GetMousePosition();
        draggingGizmoPosition = true;
    }

    if (draggingGizmoPosition)
    {
        Vector2 mouseDragEnd = GetMousePosition();
        if ( selectedGizmoArrow == 0 || selectedGizmoArrow == 1 )
        {
            float deltaY = (mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;
            selectedObjectPosition.y -= deltaY;
        }

        else if ( selectedGizmoArrow == 2 || selectedGizmoArrow == 3 )
        {
            float deltaZ = ((mouseDragEnd.x - mouseDragStart.x) + (mouseDragEnd.y - mouseDragStart.y)) * gizmoDragSensitivityFactor;
            selectedObjectPosition.z -= deltaZ;
        }
        
        else if ( selectedGizmoArrow == 4 || selectedGizmoArrow == 5 )
        {
            float deltaX = (mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
            selectedObjectPosition.x += deltaX;
        }

        mouseDragStart = mouseDragEnd;
    }

    return 0; // Success
}

void GizmoPosition()
{
    if (UpdateGizmoObjectProperties(selectedGameObjectType)) return;
    UpdateGizmoProperties();

    for (int index = 0; index < NUM_GIZMO_ARROWS; index++)
    {
        Color color1;

        isHoveringGizmo = IsGizmoBeingInteracted(gizmoArrow[index], index, color1, selectedGizmoArrow);

        if (HandleGizmoPosition() && !isHoveringGizmo) draggingGizmoPosition = false;

        Matrix rotationMat = MatrixRotateXYZ((Vector3){
            DEG2RAD * gizmoArrow[index].rotation.x,
            DEG2RAD * gizmoArrow[index].rotation.y,
            DEG2RAD * gizmoArrow[index].rotation.z
        });

        Matrix transformMatrix = MatrixIdentity();
        transformMatrix = MatrixMultiply(transformMatrix, MatrixScale(gizmoArrow[index].scale.x, gizmoArrow[index].scale.y, gizmoArrow[index].scale.z));
        transformMatrix = MatrixMultiply(transformMatrix, rotationMat);
        transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(gizmoArrow[index].position.x, gizmoArrow[index].position.y, gizmoArrow[index].position.z));
        
        gizmoArrow[index].model.transform = transformMatrix;

        DrawModel(gizmoArrow[index].model, Vector3Zero(), 1, color1);
    }

    if (selectedGameObjectType == "entity")
    {
        selectedEntity->position = selectedObjectPosition;        
        if ((bool)selectedEntity->isChild)
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

        if ((bool)selectedLight->isChild)
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

    float maxScale;
    selectedObjectPosition = selectedEntity->position;
    maxScale = std::max(std::max(selectedEntity->scale.x, selectedEntity->scale.y), selectedEntity->scale.z) / 2 + 3;
    selectedObjectScale = selectedEntity->scale; 

    UpdateGizmoProperties(maxScale);

    for (int cubeIndex = 0; cubeIndex < NUM_GIZMO_CUBES; cubeIndex++)
    {
        Color color1;

        if ((!draggingGizmoScale || !draggingGizmoRotation || !draggingGizmoPosition) && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmoCube[cubeIndex].model, sceneCamera, gizmoCube[cubeIndex].position, gizmoCube[cubeIndex].rotation, gizmoCube[cubeIndex].scale, nullptr, true);
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                selectedGizmoCube = cubeIndex;
            }
            else
            {
                color1 = { 40, 180, 40, 250 };
                selectedGizmoCube == -1;
            }
        }
        else
        {
            color1 = RED;
            selectedGizmoCube == -1;
        }


        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!draggingGizmoScale && !draggingGizmoPosition && !draggingGizmoRotation)
                {
                    mouseDragStart = GetMousePosition();
                    draggingGizmoScale = true;
                }
            }
            if (draggingGizmoScale)
            {
                Vector2 mouseDragEnd = GetMousePosition();
                float deltaX = (mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
                float deltaY = (mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;

                if (selectedGizmoCube == 0 || selectedGizmoCube == 1)
                {
                    selectedObjectScale.x += deltaY;
                }
                else if (selectedGizmoCube == 2 || selectedGizmoCube == 3)
                {
                    selectedObjectScale.y += deltaX;
                }
                else if (selectedGizmoCube == 4 || selectedGizmoCube == 5)
                {
                    selectedObjectScale.z += deltaX;
                }

                mouseDragStart = mouseDragEnd;
            }
        }
        else
        {
            draggingGizmoScale = false;
        }



        Matrix rotationMat = MatrixRotateXYZ((Vector3){
            DEG2RAD * gizmoCube[cubeIndex].rotation.x,
            DEG2RAD * gizmoCube[cubeIndex].rotation.y,
            DEG2RAD * gizmoCube[cubeIndex].rotation.z
        });

        Matrix transformMatrix = MatrixIdentity();
        transformMatrix = MatrixMultiply(transformMatrix, MatrixScale(gizmoCube[cubeIndex].scale.x, gizmoCube[cubeIndex].scale.y, gizmoCube[cubeIndex].scale.z));
        transformMatrix = MatrixMultiply(transformMatrix, rotationMat);
        transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(gizmoCube[cubeIndex].position.x, gizmoCube[cubeIndex].position.y, gizmoCube[cubeIndex].position.z));
        
        gizmoCube[cubeIndex].model.transform = transformMatrix;

        DrawModel(gizmoCube[cubeIndex].model, Vector3Zero(), 1, color1);
    }


    selectedEntity->scale = selectedObjectScale;

}


void GizmoRotation()
{
    float maxScale;
    if (selectedGameObjectType == "entity") {
        selectedObjectRotation = selectedEntity->rotation;
        selectedObjectPosition = selectedEntity->position;
        selectedObjectScale = selectedEntity->scale; 
        maxScale = std::max(std::max(selectedEntity->scale.x, selectedEntity->scale.y), selectedEntity->scale.z) / 2 + 3;
    } else if (selectedGameObjectType == "light") {
        selectedObjectRotation = { selectedLight->direction.x * 300, selectedLight->direction.y * 300, selectedLight->direction.z * 300 };
        selectedObjectPosition = { selectedLight->position.x, selectedLight->position.y, selectedLight->position.z };
        selectedObjectScale = (Vector3) { 3, 3, 3 };
        maxScale = 3;
    }

    // Update gizmo arrow positions and rotations
    for (int index = 0; index < NUM_GIZMO_TAURUS; index++)
    {
        gizmoTaurus[index].scale = { maxScale, maxScale, maxScale };
        gizmoTaurus[index].position = selectedObjectPosition;

        Color color1;

        if ((!draggingGizmoScale || !draggingGizmoRotation || !draggingGizmoPosition) && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmoTaurus[index].model, sceneCamera, gizmoTaurus[index].position, gizmoTaurus[index].rotation, gizmoTaurus[index].scale, nullptr, true);
            
            if (isHoveringGizmo)
            {
                color1 = (Color) {
                    150,
                    150,
                    150,
                    255
                };
                selectedGizmoTaurus = index;
            }
            else
            {
                color1 = (Color) {
                    100,
                    100,
                    100,
                    120
                };
                selectedGizmoTaurus = -1;
            }
        }
        else
        {
            color1 = (Color) {
                100,
                0,
                0,
                120
            };
            selectedGizmoTaurus = -1;
        }


        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!draggingGizmoScale && !draggingGizmoPosition && !draggingGizmoRotation)
                {
                    mouseDragStart = GetMousePosition();
                    draggingGizmoRotation = true;
                }
            }
            if (draggingGizmoRotation)
            {
                Vector2 mouseDragEnd = GetMousePosition();
                float deltaX = -(mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
                float deltaY = -(mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;

                // Adjust the rotation direction based on camera orientation
                Vector3 rotationDelta = {deltaY, -deltaX, 0.0f}; // Negate deltaY for correct rotation along y-axis

                // Transform rotationDelta according to camera orientation
                rotationDelta = Vector3Transform(rotationDelta, QuaternionToMatrix( (Vector4) {
                    sceneCamera.front.x,
                    sceneCamera.front.y,
                    sceneCamera.front.z,
                    0}));

                // Apply the rotation delta to the selected object's rotation
                selectedObjectRotation.x += rotationDelta.x;
                selectedObjectRotation.y += rotationDelta.y;
                selectedObjectRotation.z += rotationDelta.z;

                // Ensure rotation values are within valid range
                selectedObjectRotation.x = fmod(selectedObjectRotation.x, 360.0f);
                selectedObjectRotation.y = fmod(selectedObjectRotation.y, 360.0f);
                selectedObjectRotation.z = fmod(selectedObjectRotation.z, 360.0f);

                // Update the mouse drag start position
                mouseDragStart = mouseDragEnd;
            }
        }
        else
        {
            draggingGizmoRotation = false;
        }

        gizmoTaurus[index].rotation = selectedObjectRotation;
        Matrix rotationMat = MatrixRotateXYZ((Vector3){
            DEG2RAD * gizmoTaurus[index].rotation.x,
            DEG2RAD * gizmoTaurus[index].rotation.y,
            DEG2RAD * gizmoTaurus[index].rotation.z
        });


        Matrix transformMatrix = MatrixIdentity();
        transformMatrix = MatrixMultiply(transformMatrix, MatrixScale(gizmoTaurus[index].scale.x, gizmoTaurus[index].scale.y, gizmoTaurus[index].scale.z));
        transformMatrix = MatrixMultiply(transformMatrix, rotationMat);
        transformMatrix = MatrixMultiply(transformMatrix, MatrixTranslate(gizmoTaurus[index].position.x, gizmoTaurus[index].position.y, gizmoTaurus[index].position.z));
        
        gizmoTaurus[index].model.transform = transformMatrix;

        DrawModelWires(gizmoTaurus[index].model, Vector3Zero(), 1, color1);
    }


    if (selectedGameObjectType == "entity")
    {
        selectedEntity->rotation = selectedObjectRotation;
    }
    else if (selectedGameObjectType == "light")
    {
        selectedLight->direction.x = selectedObjectRotation.x / 300;
        selectedLight->direction.y = selectedObjectRotation.y / 300;
        selectedLight->direction.z = selectedObjectRotation.z / 300;
    }
}
