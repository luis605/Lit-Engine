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

namespace ed = ax::NodeEditor;

constexpr float SLIDER_MIN = -100.0f;
constexpr float SLIDER_MAX = 100.0f;

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
    float value  = 0;
    bool onlyInt = false;
};

struct OneMinusXNode {
    float x = 0;
};

struct MultiplyNode {
    float value = 0;
};

struct Vector2Node {
    float vec[2] = { 1.0f, 1.0f };
};

struct Node;
using NodeData = std::variant<MaterialNode, ColorNode, TextureNode, SliderNode, OneMinusXNode, MultiplyNode, Vector2Node>;

struct Pin {
    ed::PinId          ID;
    ::Node*            Node;
    std::string        Name;
    std::list<PinType> Type;
    PinKind            Kind;
    std::any           Value;

    Pin(int id, const char* name, PinType type, PinKind kind)
        : ID(id), Node(nullptr), Name(name), Kind(kind)
    {
        Type.push_back(type);
    }

    Pin(int id, const char* name, std::list<PinType> type, PinKind kind)
        : ID(id), Node(nullptr), Name(name), Type(type), Kind(kind)
    {}
};


struct Node {
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    float InputSectionWidth;
    std::string Name;
    ed::NodeId ID;
    ImColor Color;
    NodeType type;
    ImVec2 Size;
    bool isRoot = false;
    std::string UUID;

    Node(int id, const char* name, NodeType nodeType, ImColor color = ImColor(255, 255, 255), ImVec2 size = ImVec2(600, -1), float inputSectionWidth = 100.0f)
        : ID(id), Name(name), type(std::move(nodeType)), Color(std::move(color)), Size(std::move(size)), InputSectionWidth(inputSectionWidth) {
            UUID = GenUUID();
        }
};

struct Link {
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    { }
};

struct Connection {
    int nodeID;
    int pinID;
    Connection(int n, int p) : nodeID(n), pinID(p) {}
};

struct MaterialNodeSystem {
private:
    bool showAddNode = false;

public:
    int m_NextId = 1;
    std::vector<Node>    m_Nodes;
    std::vector<Link>    m_Links;
    ed::EditorContext*   m_Context          = nullptr;
    float                m_PinIconSize      = 24.0f;
    int                  draggingPinID      = -1;
    ImVec2               startPinMousePos;
    Pin*                 newLinkPin         = nullptr;

public:
    void Init() {
        m_Context = ed::CreateEditor();
    }

    int GetNextId();
    ed::LinkId GetNextLinkId();
    Node* FindNode(ed::NodeId id);
    Link* FindLink(ed::LinkId id);
    Link* FindLink(ed::PinId pinId);
    Pin* FindPin(ed::PinId id);
    bool IsPinLinked(ed::PinId id);
    bool CanCreateLink(Pin* a, Pin* b);
    void BuildNode(Node* node);
    void BuildNodes();
    void DrawNode(Node& node);
    void DrawMaterialNodeEditor();
    void ShowPopup();
    void DeleteNode(ed::NodeId nodeId);
    void DeleteLink(ed::LinkId linkId);
    void DeleteNodeLinks(ed::NodeId nodeId);
    void DeletePinLinks(ed::PinId pinId);
    Node* SpawnMaterialNode();
    Node* SpawnColorNode();
    Node* SpawnTextureNode();
    Node* SpawnSliderNode();
    Node* SpawnOneMinusXNode();
    Node* SpawnMultiplyNode();
    Node* SpawnVector2Node();
    void HandleNewLink(ed::PinId& startPinId, ed::PinId& endPinId);
    bool ArePinsValid(Pin* startPin, Pin* endPin);

};

extern Texture2D noiseTexture;
extern bool showMaterialInNodesEditor;

#endif // MATERIAL_NODE_EDITOR_H