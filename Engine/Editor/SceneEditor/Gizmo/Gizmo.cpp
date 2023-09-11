#include "Gizmo.h"
#include "../../../../include_all.h"

void InitGizmo()
{
    for (int index = 0; index < NUM_GIZMO_ARROWS; index++)
    {
        gizmo_arrow[index].model = LoadModel("assets/models/gizmo/arrow.obj");
    }

    for (int index = 0; index < NUM_GIZMO_TAURUS; index++)
    {
        gizmo_taurus[index].model = LoadModel("assets/models/gizmo/taurus.obj");
    }

    for (int index = 0; index < NUM_GIZMO_CUBES; index++)
    {
        gizmo_cube[index].model = LoadModelFromMesh(GenMeshCube(1, 1, 1));
    }
}


void GizmoPosition()
{
    Vector3 selected_object_position;

    if (selected_game_object_type == "entity")
        selected_object_position = selected_entity->position;
    else if (selected_game_object_type == "light")
        selected_object_position = {selected_light->position.x, selected_light->position.y, selected_light->position.z};

    // Gizmo Arrow Up
    gizmo_arrow[0].position = {selected_object_position.x, selected_object_position.y + 6, selected_object_position.z};
    gizmo_arrow[0].rotation = {0, 0, 0};

    // Gizmo Arrow Down
    gizmo_arrow[1].position = {selected_object_position.x, selected_object_position.y - 6, selected_object_position.z};
    gizmo_arrow[1].rotation = {180, 0, 0};

    // Gizmo Arrow Right
    gizmo_arrow[2].position = {selected_object_position.x, selected_object_position.y, selected_object_position.z + 6};
    gizmo_arrow[2].rotation = {90, 0, 0};

    // Gizmo Arrow Left
    gizmo_arrow[3].position = {selected_object_position.x, selected_object_position.y, selected_object_position.z - 6};
    gizmo_arrow[3].rotation = {-90, 0, 0};

    // Gizmo Arrow Forward
    gizmo_arrow[4].position = {selected_object_position.x + 6, selected_object_position.y, selected_object_position.z};
    gizmo_arrow[4].rotation = {0, 0, -90};

    // Gizmo Arrow Backward
    gizmo_arrow[5].position = {selected_object_position.x - 6, selected_object_position.y, selected_object_position.z};
    gizmo_arrow[5].rotation = {0, 0, 90};

    for (int arrow_i = 0; arrow_i < (sizeof(gizmo_arrow) / sizeof(gizmo_arrow[0])); arrow_i++)
    {
        Color color1;

        if ((!dragging_gizmo_scale || !dragging_gizmo_rotation || !dragging_gizmo_position) && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_arrow[arrow_i].model, scene_camera, gizmo_arrow[arrow_i].position, gizmo_arrow[arrow_i].rotation, gizmo_arrow[arrow_i].scale, nullptr, true);
            
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                gizmo_arrow_selected = arrow_i;
            }
            else
            {
                color1 = { 255, 0, 0, 100 };
                gizmo_arrow_selected == -1;
            }
        }
        else
        {
            color1 = RED;
            gizmo_arrow_selected == -1;
        }

        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!dragging_gizmo_position)
                {
                    mouse_drag_start = GetMousePosition();
                    dragging_gizmo_position = true;
                }
            }
            if (dragging_gizmo_position)
            {
                Vector2 mouse_drag_end = GetMousePosition();
                if ( gizmo_arrow_selected == 0 || gizmo_arrow_selected == 1 )
                {
                    float delta_y = (mouse_drag_end.y - mouse_drag_start.y) * gizmo_drag_sensitivity_factor;
                    
                    gizmo_arrow[0].position.y -= delta_y;
                    gizmo_arrow[1].position.y -= delta_y;
                    
                }

                else if ( gizmo_arrow_selected == 2 || gizmo_arrow_selected == 3 )
                {
                    float delta_z = ((mouse_drag_end.x - mouse_drag_start.x) + (mouse_drag_end.y - mouse_drag_start.y)) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow[arrow_i].position.z -= delta_z;
                }
                
                else if ( gizmo_arrow_selected == 4 || gizmo_arrow_selected == 5 )
                {
                    float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                    gizmo_arrow[arrow_i].position.x += delta_x;
                }

                mouse_drag_start = mouse_drag_end;
            }
        }
        else dragging_gizmo_position = false;

        float extreme_rotation = GetExtremeValue(gizmo_arrow[arrow_i].rotation);

        DrawModelEx(gizmo_arrow[arrow_i].model, gizmo_arrow[arrow_i].position, gizmo_arrow[arrow_i].rotation, extreme_rotation, {1,1,1}, color1);
    }

    float y_axis_arrows_center_pos = (gizmo_arrow[0].position.y + gizmo_arrow[1].position.y) / 2.0f;



    if (selected_game_object_type == "entity")
    {
        selected_entity->position = {
            gizmo_arrow[0].position.x,
            y_axis_arrows_center_pos,
            gizmo_arrow[0].position.z
        };
        
        if ((bool)selected_entity->isChild)
        {
            if (selected_entity->parent != nullptr && selected_entity != nullptr && selected_entity->initialized)
                selected_entity->relative_position = Vector3Subtract(selected_entity->position, selected_entity->parent->position);
        }
    }
    else if (selected_game_object_type == "light")
    {
        selected_light->position.x = gizmo_arrow[0].position.x;
        selected_light->position.y = y_axis_arrows_center_pos;
        selected_light->position.z = gizmo_arrow[0].position.z;

        if ((bool)selected_light->isChild)
        {
            auto it = std::find_if(lights_info.begin(), lights_info.end(), [selected_light](const AdditionalLightInfo& light) {
                return light.id == selected_light->id;
            });

            AdditionalLightInfo* light_info = (AdditionalLightInfo*)&*it;

            if (it != lights_info.end()) {
                if (light_info->parent != nullptr && selected_light != nullptr && light_info != nullptr)
                    selected_light->relative_position = glm::vec3(
                        selected_light->position.x - light_info->parent->position.x, 
                        selected_light->position.y - light_info->parent->position.y,
                        selected_light->position.z - light_info->parent->position.z
                        );
            }
        
        }
    }

}















void GizmoRotation()
{
    Vector3 selected_object_rotation;

    if (selected_game_object_type == "entity")
        selected_object_rotation = selected_entity->rotation;
    else if (selected_game_object_type == "light")
        selected_object_rotation = {selected_light->direction.x, selected_light->direction.y, selected_light->direction.z};

    Vector3 selected_object_position;

    if (selected_game_object_type == "entity")
        selected_object_position = selected_entity->position;
    else if (selected_game_object_type == "light")
        selected_object_position = {selected_light->position.x, selected_light->position.y, selected_light->position.z};


    Vector3 selected_object_scale;



    if (selected_game_object_type == "entity")
        selected_object_scale = {1, 1, 1};//selected_entity->scale;
    else if (selected_game_object_type == "light")
        selected_object_rotation = {1, 1, 1};


    float scale = GetExtremeValue(selected_object_scale);

    // Gizmo Arrow Up
    gizmo_taurus[0].position = selected_object_rotation;
    gizmo_taurus[0].rotation = {0, 90, 0};
    // gizmo_taurus[0].model.transform = MatrixScale(scale, scale, scale);
    
    // Gizmo Arrow Down
    gizmo_taurus[1].position = selected_object_rotation;
    gizmo_taurus[1].rotation = {90, 0, 0};
    // gizmo_taurus[1].model.transform = MatrixScale(scale, scale, scale);
    
    // Gizmo Arrow Right
    gizmo_taurus[2].position = selected_object_rotation;
    gizmo_taurus[2].rotation = {0, 0, 90};
    // gizmo_taurus[2].model.transform = MatrixScale(scale, scale, scale);


    for (int arrow_i = 0; arrow_i < (sizeof(gizmo_taurus) / sizeof(gizmo_taurus[0])) + 1; arrow_i++)
    {
        Color color1;

        if ((!dragging_gizmo_scale || !dragging_gizmo_rotation || !dragging_gizmo_position) && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_taurus[arrow_i].model, scene_camera, gizmo_taurus[arrow_i].position, gizmo_taurus[arrow_i].rotation, gizmo_taurus[arrow_i].scale, nullptr, true);
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                gizmo_taurus_selected = arrow_i;
            }
            else
            {
                color1 = { 40, 40, 255, 100 };
                gizmo_taurus_selected == -1;
            }
        }
        else
        {
            color1 = RED;
            gizmo_taurus_selected == -1;
        }


        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!dragging_gizmo_rotation)
                {
                    mouse_drag_start = GetMousePosition();
                    dragging_gizmo_rotation = true;
                }
            }
            if (dragging_gizmo_rotation)
            {
                Vector2 mouse_drag_end = GetMousePosition();
                float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                float delta_y = (mouse_drag_end.y - mouse_drag_start.y) * gizmo_drag_sensitivity_factor;

                if (gizmo_taurus_selected == 0)
                {
                    selected_object_scale.x += delta_y;
                }
                else if (gizmo_taurus_selected == 1)
                {
                    selected_object_scale.y += delta_x;
                }
                else if (gizmo_taurus_selected == 2)
                {
                    selected_object_scale.z += delta_x;
                }

                mouse_drag_start = mouse_drag_end;
            }
        }
        else
        {
            dragging_gizmo_rotation = false;
        }

        float extreme_rotation = GetExtremeValue(gizmo_taurus[arrow_i].rotation);
        DrawModelEx(gizmo_taurus[arrow_i].model, selected_object_position, gizmo_taurus[arrow_i].rotation, extreme_rotation, {1,1,1}, color1);
    }


    if (selected_game_object_type == "entity")
    {
        selected_entity->rotation = selected_object_rotation;
    }
}
















void GizmoScale()
{
    if (selected_game_object_type != "entity")
        return;

    Vector3 selected_object_position;
    Vector3 selected_object_scale;

    if (selected_game_object_type == "entity")
    {
        selected_object_position = selected_entity->position;
        selected_object_scale    = selected_entity->scale;
    }


    gizmo_cube[0].position = { selected_object_position.x + selected_object_scale.x + 1.75, selected_object_position.y, selected_object_position.z };
    gizmo_cube[1].position = { selected_object_position.x - selected_object_scale.x - 1.75, selected_object_position.y, selected_object_position.z };
    gizmo_cube[2].position = { selected_object_position.x, selected_object_position.y + selected_object_scale.y + 1.75, selected_object_position.z };
    gizmo_cube[3].position = { selected_object_position.x, selected_object_position.y - selected_object_scale.y - 1.75, selected_object_position.z };
    gizmo_cube[4].position = { selected_object_position.x, selected_object_position.y, selected_object_position.z + selected_object_scale.z + 1.75 };
    gizmo_cube[5].position = { selected_object_position.x, selected_object_position.y, selected_object_position.z - selected_object_scale.z - 1.75 };

    for (int cube_i = 0; cube_i < NUM_GIZMO_CUBES; cube_i++)
    {
        Color color1;

        if ((!dragging_gizmo_scale || !dragging_gizmo_rotation || !dragging_gizmo_position) && ImGui::IsWindowHovered())
        {
            isHoveringGizmo = IsMouseHoveringModel(gizmo_cube[cube_i].model, scene_camera, gizmo_cube[cube_i].position, gizmo_cube[cube_i].rotation, gizmo_arrow[cube_i].scale, nullptr, true);
            if (isHoveringGizmo)
            {
                color1 = GREEN;
                gizmo_cube_selected = cube_i;
                std::cout << gizmo_cube_selected << std::endl;
            }
            else
            {
                color1 = { 40, 180, 40, 200 };
                gizmo_cube_selected == -1;
            }
        }
        else
        {
            color1 = RED;
            gizmo_cube_selected == -1;
        }


        if (ImGui::IsWindowHovered() && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (isHoveringGizmo)
            {
                if (!dragging_gizmo_scale)
                {
                    mouse_drag_start = GetMousePosition();
                    dragging_gizmo_scale = true;
                }
            }
            if (dragging_gizmo_scale)
            {
                Vector2 mouse_drag_end = GetMousePosition();
                float delta_x = (mouse_drag_end.x - mouse_drag_start.x) * gizmo_drag_sensitivity_factor;
                float delta_y = (mouse_drag_end.y - mouse_drag_start.y) * gizmo_drag_sensitivity_factor;

                if (gizmo_cube_selected == 0 || gizmo_cube_selected == 1)
                {
                    selected_object_scale.x += delta_y;
                }
                else if (gizmo_cube_selected == 2 || gizmo_cube_selected == 3)
                {
                    selected_object_scale.y += delta_x;
                }
                else if (gizmo_cube_selected == 4 || gizmo_cube_selected == 5)
                {
                    selected_object_scale.z += delta_x;
                }

                mouse_drag_start = mouse_drag_end;
            }
        }
        else
        {
            dragging_gizmo_scale = false;
        }

        float extreme_rotation = GetExtremeValue(gizmo_cube[cube_i].rotation);
    
        DrawModelEx(gizmo_cube[cube_i].model, gizmo_cube[cube_i].position, gizmo_cube[cube_i].rotation, extreme_rotation, {1,1,1}, color1);
    }


    selected_entity->scale = selected_object_scale;

}