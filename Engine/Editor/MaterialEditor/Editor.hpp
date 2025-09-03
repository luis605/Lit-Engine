/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_EDITOR_HPP
#define MATERIAL_EDITOR_HPP

#include "Inspector.hpp"
#include "LayerStack.hpp"
#include "Toolbox.hpp"
#include "Viewport.hpp"
#include "LivingMaterial.hpp"

class MaterialEditor {
public:
    fs::path m_SelectedMaterialPath;
    LivingMaterial m_ActiveMaterial;

    MaterialEditor();
    ~MaterialEditor();

    void Init();
    void Render();

    void ApplyPreviewShaderSource(const std::string& fragSource);
    // TODO: Send a model to the gpu, get the UV textures of the model and use that texture to sample the rule painter area (area where a layer will appear if a rule is returing true)
    // For example, use the rule painter to draw a mask for the window and show a specific layer when rule is true (rain > 0.8)

private:
    InspectorPanel m_Inspector;
    LayerStackPanel m_LayerStack;
    ToolboxPanel m_Toolbox;
    ViewportPanel m_Viewport;
};

extern MaterialEditor materialEditor;

#endif // MATERIAL_EDITOR_HPP