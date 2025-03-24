#ifndef MATERIAL_DRAWING_H
#define MATERIAL_DRAWING_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

enum class MaterialIconType : ImU32 {
    Flow,
    Circle,
    Square,
    Grid,
    RoundSquare,
    Diamond
};

void DrawMaterialNodeIcon(ImDrawList* drawList, const ImVec2& a,
                          const ImVec2& b, MaterialIconType type, bool filled,
                          ImU32 color, ImU32 innerColor);

#endif // MATERIAL_DRAWING_H