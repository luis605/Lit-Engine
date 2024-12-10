#ifndef MATERIAL_NODE_EDITOR_H
#define MATERIAL_NODE_EDITOR_H

namespace ed = ax::NodeEditor;

bool showMaterialInNodesEditor;

enum class PinType {
    Bool,
    Number,
    TextureOrColor
};

enum class PinKind {
    Output,
    Input
};

enum class NodeType {
    Material,
    Color,
    Slider,
    Texture,
    OneMinusX
};

struct ColorNode {
    ImColor color;
};

struct TextureNode {
    Texture2D texture;
};

struct Node;
using NodeData = std::variant<ColorNode, TextureNode>;

struct Pin {
    ed::PinId   ID;
    ::Node*     Node;
    std::string Name;
    PinType     Type;
    PinKind     Kind;

    Pin(int id, const char* name, PinType type, PinKind kind):
        ID(id), Node(nullptr), Name(name), Type(type), Kind(kind)
    {}
};

struct Node {
    ed::NodeId ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    ImVec2 Size;
    NodeType type;
    NodeData data;

    Node(int id, const char* name, NodeData nodeData, NodeType nodeType, ImColor color = ImColor(255, 255, 255))
        : ID(id), Name(name), data(std::move(nodeData)), type(std::move(nodeType)), Color(color), Size(600, -1) {}
};

template <typename T>
std::optional<T*> GetNodeData(Node& node) {
    return std::visit(
        [](auto& data) -> std::optional<T*> {
            using U = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<U, T>) {
                return &data;
            } else {
                return std::nullopt;
            }
        },
        node.data);
}

struct Link {
    ed::LinkId ID;

    ed::PinId StartPinID;
    ed::PinId EndPinID;

    ImColor Color;

    Link(ed::LinkId id, ed::PinId startPinId, ed::PinId endPinId):
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255)
    { }
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
    Pin* FindPin(ed::PinId id);
    bool IsPinLinked(ed::PinId id);
    bool CanCreateLink(Pin* a, Pin* b);
    void BuildNode(Node* node);
    void BuildNodes();
    void DrawNodeMiddleSection(Node& node);
    void DrawNode(Node& node);
    void DrawMaterialNodeEditor(SurfaceMaterial& surfaceMaterial);
    void ShowPopup();
    Node* SpawnMaterialNode();
    Node* SpawnColorNode();
    Node* SpawnTextureNode();
    Node* SpawnSliderNode();
    Node* SpawnOneMinusXNode();
    Node* SpawnMultiplyNode();
    void HandleNewLink(ed::PinId& startPinId, ed::PinId& endPinId);
    bool ArePinsValid(Pin* startPin, Pin* endPin);

};

MaterialNodeSystem materialNodeSystem;
Texture2D noiseTexture;

#endif // MATERIAL_NODE_EDITOR_H