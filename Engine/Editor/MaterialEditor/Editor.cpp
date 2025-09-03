/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Editor.hpp"
#include "MaterialLibrary.hpp"

MaterialEditor::MaterialEditor() {
    MaterialLibrary::GetInstance().LoadLibrary("Assets/materials.json");
    m_ActiveMaterial.m_Name = "MyLivingMaterial";
}

MaterialEditor::~MaterialEditor() {
}

void MaterialEditor::Init() {
    m_Viewport.Init();
}

void MaterialEditor::Render() {
    m_Viewport.Render();
    m_LayerStack.Render(m_ActiveMaterial);
    m_Inspector.Render(m_ActiveMaterial, m_LayerStack.GetSelectedLayer());
    m_Toolbox.Render();
}

void MaterialEditor::ApplyPreviewShaderSource(const std::string& fragSource) {
    m_Viewport.ApplyPreviewFragmentShader(fragSource);
}

MaterialEditor materialEditor;