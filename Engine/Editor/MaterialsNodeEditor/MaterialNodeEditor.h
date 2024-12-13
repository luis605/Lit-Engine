#ifndef MATERIAL_NODE_EDITOR_H
#define MATERIAL_NODE_EDITOR_H

namespace ed = ax::NodeEditor;

bool showMaterialInNodesEditor;
constexpr float SLIDER_MIN = -100.0f;
constexpr float SLIDER_MAX = 100.0f;

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
    Texture,
    Slider,
    OneMinusX,
    Multiply
};

struct ColorNode {
    ImColor color;
};

struct TextureNode {
    SurfaceMaterialTexture texture;
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

struct Node;
using NodeData = std::variant<ColorNode, TextureNode, SliderNode, OneMinusXNode, MultiplyNode>;

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
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    float InputSectionWidth;
    std::string Name;
    ed::NodeId ID;
    ImColor Color;
    NodeType type;
    NodeData data;
    ImVec2 Size;

    Node(int id, const char* name, NodeData nodeData, NodeType nodeType, ImColor color = ImColor(255, 255, 255), ImVec2 size = ImVec2(600, -1), float inputSectionWidth = 100.0f)
        : ID(id), Name(name), data(std::move(nodeData)), type(std::move(nodeType)), Color(std::move(color)), Size(std::move(size)), InputSectionWidth(inputSectionWidth) {}
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
    void DrawNodeMiddleSection(Node& node, const ImVec2& cursorStartPos);
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