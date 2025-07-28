/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_NODE_EDITOR_H
#define MATERIAL_NODE_EDITOR_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <NodeEditor/imgui_node_editor.h>
#include <NodeEditor/imgui_canvas.h>
#include <NodeEditor/examples/application/include/application.h>
#include <Engine/Lighting/SurfaceMaterial.hpp>
#include <Engine/Core/UUID.hpp>
#include <any>
#include <list>
#include <variant>
#include <vector>
#include <optional>
#include <unordered_map>

namespace ed = ax::NodeEditor;

namespace std {
    template <>
    struct hash<ed::NodeId> {
        size_t operator()(const ed::NodeId& id) const {
            return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(id.AsPointer()));
        }
    };

    template <>
    struct hash<ed::LinkId> {
        size_t operator()(const ed::LinkId& id) const {
            return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(id.AsPointer()));
        }
    };

    template <>
    struct hash<ed::PinId> {
        size_t operator()(const ed::PinId& id) const {
            return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(id.AsPointer()));
        }
    };
} // namespace std

enum class PinType {
    Bool,
    Number,
    TextureOrColor,
    Vector2
};

enum class PinKind {
    Output,
    Input
};

enum class NodeType {
    Material,
    Color,
    Texture,
    Slider,
    OneMinusX,
    Multiply,
    Vector2
};

struct ColorNode {
    ImColor color;
};

struct MaterialNode {
    SurfaceMaterialTexture diffuse;
    SurfaceMaterialTexture normal;
    SurfaceMaterialTexture roughness;
    SurfaceMaterialTexture ambientOcclusion;
    SurfaceMaterialTexture height;
    SurfaceMaterialTexture metallic;
    SurfaceMaterialTexture emissive;
    alignas(4) float clearCoat = 0.0f;
};

struct TextureNode {
    SurfaceMaterialTexture texture;
    fs::path texturePath;
};

struct SliderNode {
    float value = 0.0f;
    bool onlyInt = false;
};

struct OneMinusXNode {
    float x = 0.0f;
};

struct MultiplyNode {
    float value = 0.0f;
};

struct Vector2Node {
    float vec[2] = {1.0f, 1.0f};
};

struct Node;
using NodeData = std::variant<MaterialNode, ColorNode, TextureNode, SliderNode, OneMinusXNode, MultiplyNode, Vector2Node>;

struct Pin {
    ed::PinId m_id;
    ed::NodeId m_ownerId;
    std::string m_name;
    std::list<PinType> m_type;
    PinKind m_kind;
    std::any m_value;

    Pin(int id, ed::NodeId ownerId, const char* name, PinType type, PinKind kind)
        : m_id(id), m_ownerId(ownerId), m_name(name), m_kind(kind) {
        m_type.push_back(type);
    }

    Pin(int id, ed::NodeId ownerId, const char* name, std::list<PinType> type, PinKind kind)
        : m_id(id), m_ownerId(ownerId), m_name(name), m_type(std::move(type)), m_kind(kind) {}
};

struct Node {
    std::vector<ed::PinId> m_inputs;
    std::vector<ed::PinId> m_outputs;
    float m_inputSectionWidth;
    std::string m_name;
    ed::NodeId m_id;
    ImColor m_color;
    NodeType m_type;
    ImVec2 m_size;
    bool m_isRoot = false;
    std::string m_uuid;
    std::string m_renameBuffer;
    NodeData m_data;

    Node(int id, const char* name, NodeType nodeType, NodeData data, ImColor color = ImColor(255, 255, 255), ImVec2 size = ImVec2(600, -1), float inputSectionWidth = 100.0f)
        : m_id(id), m_name(name), m_type(std::move(nodeType)), m_data(std::move(data)), m_color(std::move(color)), m_size(std::move(size)), m_inputSectionWidth(inputSectionWidth) {
        m_uuid = GenUUID();
        m_renameBuffer = m_name;
    }
};

struct Link {
    ed::LinkId m_id;
    ed::PinId m_startPinId;
    ed::PinId m_endPinId;
    ImColor m_color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId)
        : m_id(id), m_startPinId(startPinId), m_endPinId(endPinId), m_color(255, 255, 255) {}
};

class MaterialNodeSystem {
public:
    void Initialize();
    void Shutdown();
    void DrawEditor();
    int GetNextId();
    ed::LinkId GetNextLinkId();

    Node* FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    Pin* FindPin(ed::PinId id);
    bool IsPinLinked(ed::PinId id);
    void AddPin(Node& node, Pin&& pin);
    void AddLink(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId);
    Node* SpawnMaterialNode();
    Node* SpawnColorNode();
    Node* SpawnTextureNode();
    Node* SpawnSliderNode();
    Node* SpawnOneMinusXNode();
    Node* SpawnMultiplyNode();
    Node* SpawnVector2Node();

    std::unordered_map<ed::NodeId, Node> m_nodes;
    std::unordered_map<ed::LinkId, Link> m_links;
    std::unordered_map<ed::PinId, Pin> m_pins;
    ed::EditorContext* m_context = nullptr;

private:
    void BuildNode(Node* node);
    void DrawNode(Node& node);
    void ShowNodeContextMenu();
    void ShowBackgroundContextMenu();

    void DeleteNode(ed::NodeId nodeId);
    void DeleteLink(ed::LinkId linkId);

    void HandleConnection(ed::PinId startPinId, ed::PinId endPinId);
    bool ArePinsCompatible(Pin* startPin, Pin* endPin);

    int m_nextId = 1;
    float m_pinIconSize = 24.0f;
    ed::NodeId m_contextNodeId = 0;
    bool m_openContextMenu = false;
};

extern Texture2D noiseTexture;
extern bool showMaterialInNodesEditor;

#endif // MATERIAL_NODE_EDITOR_H