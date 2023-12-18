#pragma once
#include "../../../include_all.h"
#include "MaterialsNodeEditor.h"
#include "Nodes.cpp"





std::map<std::string, MyNode*(*)()> available_nodes{
    {"Color", []() -> MyNode* {
        return new MyNode("Color", {}, {
            {"Color", NodeSlotColor} 
        });
    }},
    {"Texture", []() -> MyNode* {
        return new MyNode("Texture", {
            {"Diffuse Map", NodeSlotDiffuseTexture},
            {"Normal Map", NodeSlotNormalTexture},
            {"Roughness Map", NodeSlotRoughnessTexture},
            {"AO Map", NodeSlotAoTexture},
        },
        {
            {"Texture", NodeSlotTexture},
        });
    }},
    {"Diffuse Texture", []() -> MyNode* {
        return new MyNode("Diffuse Texture", {},
        {
            {"Diffuse Texture", NodeSlotDiffuseTexture},
        });
    }},
    {"Normal Map Texture", []() -> MyNode* {
        return new MyNode("Normal Map Texture", {},
        {
            {"Normal Map Texture", NodeSlotNormalTexture},
        });
    }},
    {"Roughness Map Texture", []() -> MyNode* {
        return new MyNode("Roughness Map Texture", {},
        {
            {"Roughness Map Texture", NodeSlotRoughnessTexture},
        });
    }},
    {"Ambient Occlusion Map Texture", []() -> MyNode* {
        return new MyNode("Ambient Occlusion Map Texture", {},
        {
            {"AO Map Texture", NodeSlotAoTexture},
        });
    }},
    {"Surface Material", []() -> MyNode* {
        return new MyNode("Surface Material", {}, {
            {"Output", NodeSlotSurfaceMaterial},
        });
    }},
    {"Material", []() -> MyNode* {
        return new MyNode("Material", {
            {"Color", NodeSlotColor},
            {"Texture", NodeSlotTexture},
            {"Surface Material", NodeSlotSurfaceMaterial}
        }, {});
    }},

};
std::vector<MyNode*> nodes;




void SetMaterial(SurfaceMaterial& material)
{
    // Color
    selected_entity->surface_material.color = entity_material.color;

    // Textures
    if (IsTextureReady(entity_material.texture))
    {
        Texture2D entity_texture = entity_material.texture;
        selected_entity->texture = entity_texture;
        selected_entity->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = entity_texture;
        selected_entity->texture_path = entity_material.texture_path.string();
    }

    if (IsTextureReady(entity_material.normal_texture))
    {
        selected_entity->normal_texture = entity_material.normal_texture;

        if (auto normal = get_if<Texture2D>(&selected_entity->normal_texture)) {
            selected_entity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = *normal;
        } else if (auto* videoPlayerPtr = std::get_if<std::unique_ptr<VideoPlayer>>(&selected_entity->normal_texture)) {
            (*videoPlayerPtr)->Update();
            selected_entity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture = (*videoPlayerPtr)->GetTexture();
        }

        selected_entity->normal_texture_path = entity_material.normal_texture_path.string();
    }
    
    // Surface Material
    selected_entity->surface_material.shininess = entity_material.shininess;
    selected_entity->surface_material.SpecularIntensity = entity_material.SpecularIntensity;
    selected_entity->surface_material.Roughness = entity_material.Roughness;
    selected_entity->surface_material.DiffuseIntensity = entity_material.DiffuseIntensity;
    selected_entity->surface_material.SpecularTint = entity_material.SpecularTint;


    material.shininess = entity_material.shininess;
    material.SpecularIntensity = entity_material.SpecularIntensity;
    material.Roughness = entity_material.Roughness;
    material.DiffuseIntensity = entity_material.DiffuseIntensity;
    material.SpecularTint = entity_material.SpecularTint;
}



void MaterialsNodeEditor(SurfaceMaterial& material)
{
    if (!show_material_in_nodes_editor)
        return;

    static ImNodes::Ez::Context* context = ImNodes::Ez::CreateContext();
    IM_UNUSED(context);

    if (ImGui::Begin("Material Node Editor"))
    {

        ImNodes::Ez::BeginCanvas();

        can_apply_material = false;

        for (auto it = nodes.begin(); it != nodes.end();)
        {
            MyNode* node = *it;

            if (ImNodes::Ez::BeginNode(node, node->Title, &node->Pos, &node->Selected))
            {
                ImNodes::Ez::InputSlots(node->InputSlots.data(), node->InputSlots.size());



                ColorNode(node);
                TextureNode(node);
                NormalMapTextureNode(node);
                SurfaceMaterialNode(node);
                MaterialNode(node);

                // Render output nodes first (order is important)
                ImNodes::Ez::OutputSlots(node->OutputSlots.data(), node->OutputSlots.size());

                // Store new connections when they are created
                Connection new_connection;
                if (ImNodes::GetNewConnection(&new_connection.InputNode, &new_connection.InputSlot,
                                              &new_connection.OutputNode, &new_connection.OutputSlot))
                {
                    ((MyNode*) new_connection.InputNode)->Connections.push_back(new_connection);
                    ((MyNode*) new_connection.OutputNode)->Connections.push_back(new_connection);
                }

                // Render output connections of this node
                for (const Connection& connection : node->Connections)
                {
                    // Node contains all it's connections (both from output and to input slots). This means that multiple
                    // nodes will have same connection. We render only output connections and ensure that each connection
                    // will be rendered once.
                    if (connection.OutputNode != node)
                        continue;

                    if (!ImNodes::Connection(connection.InputNode, connection.InputSlot, connection.OutputNode,
                                             connection.OutputSlot))
                    {
                        // Remove deleted connections
                        ((MyNode*) connection.InputNode)->DeleteConnection(connection);
                        ((MyNode*) connection.OutputNode)->DeleteConnection(connection);
                    }
                }
            }
            // Node rendering is done. This call will render node background based on size of content inside node.
            ImNodes::Ez::EndNode();

            if (node->Selected && ImGui::IsKeyPressedMap(ImGuiKey_Delete) && ImGui::IsWindowFocused())
            {
                // Deletion order is critical: first we delete connections to us
                for (auto& connection : node->Connections)
                {
                    if (connection.OutputNode == node)
                    {
                        ((MyNode*) connection.InputNode)->DeleteConnection(connection);
                    }
                    else
                    {
                        ((MyNode*) connection.OutputNode)->DeleteConnection(connection);
                    }
                }
                // Then we delete our own connections, so we don't corrupt the list
                node->Connections.clear();
                
                delete node;
                it = nodes.erase(it);
            }
            else
                ++it;
        }

        if (ImGui::IsMouseReleased(1) && ImGui::IsWindowHovered() && !ImGui::IsMouseDragging(1))
        {
            ImGui::FocusWindow(ImGui::GetCurrentWindow());
            ImGui::OpenPopup("NodesContextMenu");
        }

        if (ImGui::BeginPopup("NodesContextMenu"))
        {
            for (const auto& desc : available_nodes)
            {
                if (ImGui::MenuItem(desc.first.c_str()))
                {
                    nodes.push_back(desc.second());
                    ImNodes::AutoPositionNode(nodes.back());
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Zoom"))
                ImNodes::GetCurrentCanvas()->Zoom = 1;

            if (ImGui::IsAnyMouseDown() && !ImGui::IsWindowHovered())
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }



        if (can_apply_material)
        {
            ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            ImVec2 buttonSize(100, 25);
            ImVec2 buttonPos(canvasPos.x + canvasSize.x - buttonSize.x, canvasPos.y + canvasSize.y - buttonSize.y);
            ImGui::SetCursorScreenPos(buttonPos);


            if (ImGui::Button("Apply", buttonSize))
            {
                SetMaterial(material);
            }
        }

        ImNodes::Ez::EndCanvas();
    }
    ImGui::End();
}