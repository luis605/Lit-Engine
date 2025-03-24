#ifndef MATERIAL_HELPERS_H
#define MATERIAL_HELPERS_H

#include <Engine/Editor/MaterialsNodeEditor/MaterialNodeEditor.hpp>
#include <NodeEditor/imgui_node_editor.h>
#include <vector>

void DrawTextInNodeEditor(const char* label, bool isWarning);
std::vector<ed::PinId> GetConnectedInputPins(ed::PinId outputPinId);
std::vector<Pin*> FindConnectedPins(const Node& materialNode,
                                    const std::vector<Link>& links);
int FindNodeByPinID(const std::vector<Node>& nodes, int pinID);

#endif // MATERIAL_HELPERS_H