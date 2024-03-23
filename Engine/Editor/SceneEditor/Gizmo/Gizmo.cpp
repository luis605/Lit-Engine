#include "Gizmo.h"
#include "../../../../include_all.h"

void InitGizmo()
{
    for (int index = 0; index < NUM_GIZMO_ARROWS; index++)
    {
        gizmoArrow[index].model = LoadModel("assets/models/gizmo/arrow.obj");
        gizmoArrow[index].rotation = gizmoArrowOffsets[index].rotation;
        gizmoArrow[index].model.materials[0].shader = shader;
    }

    for (int index = 0; index < NUM_GIZMO_TAURUS; index++)
    {
        gizmoTaurus[index].model = LoadModelFromMesh(GenMeshSphere(1, 30, 30));
        gizmoTaurus[index].model.materials[0].shader = shader;
    }

    gizmoTaurus[0].rotation = {0, 0, 0};

    for (int index = 0; index < NUM_GIZMO_CUBES; index++)
    {
        gizmoCube[index].model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
        gizmoCube[index].model.materials[0].shader = shader;

    }
}


void GizmoPosition()
{
    Vector3 selectedObjectPosition;

    if (selectedGameObjectType == "entity" && selectedEntity != nullptr)
        selectedObjectPosition = selectedEntity->position;
    else if (selectedGameObjectType == "light" && selectedLight != nullptr)
        selectedObjectPosition = {selectedLight->position.x, selectedLight->position.y, selectedLight->position.z};
    else
        return;
    
    // Update gizmo arrow positions and rotations
    for (int index = 0; index < 6; ++index) {
        gizmoArrow[index].position = Vector3Add(selectedObjectPosition, gizmoArrowOffsets[index].position);
    }

    for (int index = 0; index < (sizeof(gizmoArrow) / sizeof(gizmoArrow[0])); index++)
    {
        Color color1 = RED;

        if (!dragging && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmoArrow[index].model, sceneCamera, gizmoArrow[index].position, gizmoArrow[index].rotation, gizmoArrow[index].scale, nullptr, true);
            
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                selectedGizmoArrow = index;
            }
            else selectedGizmoArrow == -1;
        }
        else selectedGizmoArrow == -1;

        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!draggingGizmoPosition)
                {
                    mouseDragStart = GetMousePosition();
                    draggingGizmoPosition = true;
                }
            }
            if (draggingGizmoPosition)
            {
                Vector2 mouseDragEnd = GetMousePosition();
                if ( selectedGizmoArrow == 0 || selectedGizmoArrow == 1 )
                {
                    float deltaY = (mouseDragEnd.y - mouseDragStart.y) * gizmoDragSensitivityFactor;
                    
                    gizmoArrow[0].position.y -= deltaY;
                    gizmoArrow[1].position.y -= deltaY;
                    
                }

                else if ( selectedGizmoArrow == 2 || selectedGizmoArrow == 3 )
                {
                    float deltaZ = ((mouseDragEnd.x - mouseDragStart.x) + (mouseDragEnd.y - mouseDragStart.y)) * gizmoDragSensitivityFactor;
                    gizmoArrow[index].position.z -= deltaZ;
                }
                
                else if ( selectedGizmoArrow == 4 || selectedGizmoArrow == 5 )
                {
                    float deltaX = (mouseDragEnd.x - mouseDragStart.x) * gizmoDragSensitivityFactor;
                    gizmoArrow[index].position.x += deltaX;
                }

                mouseDragStart = mouseDragEnd;
            }
        }
        else draggingGizmoPosition = false;

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

    float yAxisArrowsCenterPos = (gizmoArrow[0].position.y + gizmoArrow[1].position.y) / 2.0f;

    if (selectedGameObjectType == "entity")
    {
        selectedEntity->position = {
            gizmoArrow[0].position.x,
            yAxisArrowsCenterPos,
            gizmoArrow[0].position.z
        };
        
        if ((bool)selectedEntity->isChild)
        {
            if (selectedEntity->parent != nullptr && selectedEntity != nullptr && selectedEntity->initialized)
                selectedEntity->relativePosition = Vector3Subtract(selectedEntity->position, selectedEntity->parent->position);
        }
    }
    else if (selectedGameObjectType == "light")
    {
        selectedLight->position.x = gizmoArrow[0].position.x;
        selectedLight->position.y = yAxisArrowsCenterPos;
        selectedLight->position.z = gizmoArrow[0].position.z;

        if ((bool)selectedLight->isChild)
        {
            auto it = std::find_if(lightsInfo.begin(), lightsInfo.end(), [selectedLight](const AdditionalLightInfo& light) {
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


void GizmoRotation()
{
    Vector3 selectedObjectRotation;

    if (selectedGameObjectType == "entity") {
        selectedObjectRotation = selectedEntity->rotation;
        float maxScale = std::max(std::max(selectedEntity->scale.x, selectedEntity->scale.y), selectedEntity->scale.z);
        selectedObjectScale = (Vector3) { maxScale, maxScale, maxScale }; 
    } else if (selectedGameObjectType == "light") {
        selectedObjectRotation = {selectedLight->direction.x, selectedLight->direction.y, selectedLight->direction.z};
        selectedObjectScale = (Vector3) { 1, 1, 1 };
    }

    if (selectedGameObjectType == "entity")
        selectedObjectPosition = selectedEntity->position;
    else if (selectedGameObjectType == "light")
        selectedObjectPosition = {selectedLight->position.x, selectedLight->position.y, selectedLight->position.z};

    if (selectedGameObjectType == "entity" && selectedEntity != nullptr)
    {
        selectedObjectRotation = (Vector3) {
            selectedEntity->rotation.x,
            selectedEntity->rotation.y,
            selectedEntity->rotation.z
        };
    }
    else if (selectedGameObjectType == "light")
    {
        selectedObjectRotation = (Vector3) {
            selectedLight->direction.x,
            selectedLight->direction.y,
            selectedLight->direction.z
        };
    }

    // Update gizmo arrow positions and rotations
    for (int index = 0; index < NUM_GIZMO_TAURUS; index++)
    {
        gizmoTaurus[index].scale = selectedObjectScale;
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
                    120
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

                mouseDragStart = mouseDragEnd;
            }
        }
        else
        {
            draggingGizmoRotation = false;
        }

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
}
















void GizmoScale()
{
    if (selectedGameObjectType != "entity")
        return;

    Vector3 selectedObjectPosition;
    Vector3 selectedObjectScale;

    if (selectedGameObjectType == "entity")
    {
        selectedObjectPosition = selectedEntity->position;
        float maxScale = std::max(std::max(selectedEntity->scale.x, selectedEntity->scale.y), selectedEntity->scale.z);
        selectedObjectScale = (Vector3) { maxScale, maxScale, maxScale }; 
    }

    gizmoCube[0].position = { selectedObjectPosition.x + selectedObjectScale.x + 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[1].position = { selectedObjectPosition.x - selectedObjectScale.x - 3.75f, selectedObjectPosition.y, selectedObjectPosition.z };
    gizmoCube[2].position = { selectedObjectPosition.x, selectedObjectPosition.y + selectedObjectScale.y + 3.75f, selectedObjectPosition.z };
    gizmoCube[3].position = { selectedObjectPosition.x, selectedObjectPosition.y - selectedObjectScale.y - 3.75f, selectedObjectPosition.z };
    gizmoCube[4].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z + selectedObjectScale.z + 3.75f };
    gizmoCube[5].position = { selectedObjectPosition.x, selectedObjectPosition.y, selectedObjectPosition.z - selectedObjectScale.z - 3.75f };

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