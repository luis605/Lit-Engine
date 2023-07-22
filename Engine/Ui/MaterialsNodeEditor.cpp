#include "../../include_all.h"
#include "MaterialsNodeEditor.h"




struct Connection
{
    /// `id` that was passed to BeginNode() of input node.
    void* InputNode = nullptr;
    /// Descriptor of input slot.
    const char* InputSlot = nullptr;
    /// `id` that was passed to BeginNode() of output node.
    void* OutputNode = nullptr;
    /// Descriptor of output slot.
    const char* OutputSlot = nullptr;

    bool operator==(const Connection& other) const
    {
        return InputNode == other.InputNode &&
               InputSlot == other.InputSlot &&
               OutputNode == other.OutputNode &&
               OutputSlot == other.OutputSlot;
    }

    bool operator!=(const Connection& other) const
    {
        return !operator ==(other);
    }
};

enum NodeSlotTypes
{
    NodeSlotColor = 1,   // ID can not be 0
    NodeSlotTexture = 2,
    NodeSlotNormalTexture = 3,
};

/// A structure holding node state.
struct MyNode
{
    /// Title which will be displayed at the center-top of the node.
    const char* Title = nullptr;
    /// Flag indicating that node is selected by the user.
    bool Selected = false;
    /// Node position on the canvas.
    ImVec2 Pos{};
    /// List of node connections.
    std::vector<Connection> Connections{};
    /// A list of input slots current node has.
    std::vector<ImNodes::Ez::SlotInfo> InputSlots{};
    /// A list of output slots current node has.
    std::vector<ImNodes::Ez::SlotInfo> OutputSlots{};

    ImVec4 ColorValue = {
        static_cast<float>(selected_entity->color.r) / 255.0f,
        static_cast<float>(selected_entity->color.g) / 255.0f,
        static_cast<float>(selected_entity->color.b) / 255.0f,
        static_cast<float>(selected_entity->color.a) / 255.0f
    };


    explicit MyNode(const char* title,
        const std::vector<ImNodes::Ez::SlotInfo>&& input_slots,
        const std::vector<ImNodes::Ez::SlotInfo>&& output_slots)
    {
        Title = title;
        InputSlots = input_slots;
        OutputSlots = output_slots;
    }

    /// Deletes connection from this node.
    void DeleteConnection(const Connection& connection)
    {
        for (auto it = Connections.begin(); it != Connections.end(); ++it)
        {
            if (connection == *it)
            {
                Connections.erase(it);
                break;
            }
        }
    }
};

std::map<std::string, MyNode*(*)()> available_nodes{
    {"Color", []() -> MyNode* {
        return new MyNode("Color", {}, {
            {"Color", NodeSlotColor} 
        });
    }},
    {"Texture", []() -> MyNode* {
        return new MyNode("Texture", {},
        {
            {"Texture", NodeSlotTexture},
            {"Normal Map", NodeSlotNormalTexture},
        });
    }},
    {"Material", []() -> MyNode* {
        return new MyNode("Material", {
            {"Color", NodeSlotColor},
            {"Texture", NodeSlotTexture}
        }, {});
    }},

};
std::vector<MyNode*> nodes;


struct EntityMaterial
{
    Color color;
};

EntityMaterial entity_material;


void SetMaterial()
{
    selected_entity->color = entity_material.color;
}



void MaterialsNodeEditor()
{
    if (!show_material_in_nodes_editor)
        return;

    static ImNodes::Ez::Context* context = ImNodes::Ez::CreateContext();
    IM_UNUSED(context);

    if (ImGui::Begin("Material Node Editor"))
    {

        ImNodes::Ez::BeginCanvas();

        bool can_apply_material = false;

        for (auto it = nodes.begin(); it != nodes.end();)
        {
            MyNode* node = *it;

            // Start rendering node
            if (ImNodes::Ez::BeginNode(node, node->Title, &node->Pos, &node->Selected))
            {
                ImNodes::Ez::InputSlots(node->InputSlots.data(), node->InputSlots.size());

                if (node->Title == "Color")
                {
                    ImGui::SetNextItemWidth(300);
                    ImGui::ColorEdit4("Color Picker", (float*)&node->ColorValue);
                }


                if (node->Title == "Texture")
                {
                    // Get the initial cursor position
                    ImVec2 initialCursorPos = ImGui::GetCursorPos();

                    for (const ImNodes::Ez::SlotInfo& outputSlot : node->OutputSlots)
                    {
                        if (outputSlot.title == "Texture")
                        {
                            ImGui::ImageButton((ImTextureID)&selected_entity->texture, ImVec2(100, 100));
                        }
                        else if (outputSlot.title == "Normal Map")
                        {
                            ImGui::ImageButton((ImTextureID)&selected_entity->normal_texture, ImVec2(100, 100));
                        }
                    }
                }


                if (node->Title == "Material")
                {
                    can_apply_material = true;
                    for (const Connection& connection : node->Connections)
                    {
                        if (connection.InputSlot == "Color")
                        {
                            ImVec4 colorValue = ((MyNode*)(connection.OutputNode))->ColorValue;
                            ImGui::ColorButton("Connected Color", colorValue, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip);
                            entity_material.color = { colorValue.x * 255, colorValue.y * 255, colorValue.z * 255, colorValue.w * 255 };
                        }
                    }
                }

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
                SetMaterial();
            }
        }

        ImNodes::Ez::EndCanvas();
    }
    ImGui::End();
}