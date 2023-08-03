#ifndef MATERIAL_H
#define MATERIAL_H

bool show_material_in_nodes_editor = false;
bool can_apply_material = false;

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
    NodeSlotSurfaceMaterial = 4,
};



struct EntityMaterial
{
    // Colors
    Color color;

    // Textures
    Texture2D texture;
    std::filesystem::path texture_path;
    Texture2D normal_texture;
    std::filesystem::path normal_texture_path;
    
    // Surface Material
    float shininess = 0.5f;
    float SpecularIntensity = 0.5f;
    float Roughness = 0.5f;
    float DiffuseIntensity = 0.5f;
    alignas(16) glm::vec3 SpecularTint = { 1.0f, 0.2f, 0.1f };

};

EntityMaterial entity_material;


/// A structure holding node state.
struct MyNode
{
    const char* Title = nullptr;
    bool Selected = false;
    ImVec2 Pos{};

    std::vector<Connection> Connections{};
    std::vector<ImNodes::Ez::SlotInfo> InputSlots{};
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

        entity_material.color = selected_entity->color;
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






#endif // MATERIAL_H