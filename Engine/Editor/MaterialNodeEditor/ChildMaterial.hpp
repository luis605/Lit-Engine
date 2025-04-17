#ifndef CHILD_MATERIAL_HPP
#define CHILD_MATERIAL_HPP

#include <Engine/Core/UUID.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialBlueprints.hpp>
#include <Engine/Editor/MaterialNodeEditor/MaterialNodeEditor.hpp>
#include <NodeEditor/imgui_node_editor.h>
#include <unordered_map>
#include <filesystem>
#include <string>
#include <variant>

namespace fs = std::filesystem;
namespace ed = ax::NodeEditor;

struct ChildMaterial {
    std::string name;
    fs::path path;
    fs::path blueprintPath;
    std::unordered_map<std::string, NodeData> nodes;
    std::string materialBlueprintUUID;

    template <typename T>
    std::optional<T*> GetNodeData(const std::string& uuid);
    void SyncWithBlueprint();
};

extern std::unordered_map<fs::path, ChildMaterial> childMaterials;

void SaveChildMaterial(const fs::path& path, const ChildMaterial& childMaterial);
void LoadChildMaterial(const fs::path& path);

#endif // CHILD_MATERIAL_HPP