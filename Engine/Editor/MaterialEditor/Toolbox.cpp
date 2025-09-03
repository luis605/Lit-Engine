/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include <Engine/Core/Core.hpp>
#include "Toolbox.hpp"
#include "imgui.h"
#include "Editor.hpp"
#include "MaterialFile.hpp"
#include "ShaderGenerator.hpp"
#include "ShaderCache.hpp"

ToolboxPanel::ToolboxPanel() {}
ToolboxPanel::~ToolboxPanel() {}

void ToolboxPanel::Render() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
    ImGui::Begin("Toolbox");
    ImGui::PopStyleVar();

    ImGui::TextUnformatted("Tools");
    ImGui::Separator();
    ImGui::Button("Material Brush", ImVec2(-1, 0));
    ImGui::Button("Rule Painter", ImVec2(-1, 0));
    ImGui::Button("Decal Placer", ImVec2(-1, 0));

    ImGui::Separator();

    if (ImGui::Button("Back to Scene Editor", ImVec2(-1, 0))) {
        currentEditorState = EditorState::SceneEditor;
    }

    if (ImGui::Button("Save Material", ImVec2(-1, 0))) {
        fs::path path = materialEditor.m_SelectedMaterialPath;
        if (path.empty()) {
            fs::path folder = fs::path("Assets/Materials");
            fs::create_directories(folder);
            std::string fname = materialEditor.m_ActiveMaterial.m_Name.empty() ? "Unnamed" : materialEditor.m_ActiveMaterial.m_Name;
            path = folder / (fname + ".json");
            materialEditor.m_SelectedMaterialPath = path;
        }
        MaterialFile::Save(path, materialEditor.m_ActiveMaterial);
        m_LastCompileLog = "Material saved to: " + path.string();
    }

    if (ImGui::Button("Compile Material Shader", ImVec2(-1, 0))) {
        ShaderGenReport report;
        std::string src = GenerateFragmentShader(materialEditor.m_ActiveMaterial, &report);
        if (src.empty()) {
            m_LastCompileLog = "Codegen failed: empty shader. Errors:\n";
            for (const auto& e : report.errors) m_LastCompileLog += "  - " + e + "\n";
            m_LastShaderSource.clear();
        } else {
            auto cacheRes = CacheCompiledShader(materialEditor.m_SelectedMaterialPath, materialEditor.m_ActiveMaterial, src, report.codegenVersion);
            if (!cacheRes.ok) {
                m_LastCompileLog = "Failed to cache shader: " + cacheRes.error;
            } else {
                m_LastCompileLog = "Shader compiled and cached.\n";
                m_LastCompileLog += "  Hash: " + cacheRes.hashHex + "\n";
                m_LastCompileLog += "  Path: " + cacheRes.cachedPath.string() + "\n";
                m_LastCompileLog += "  Layers: " + std::to_string(report.numLayers) + ", Samplers: " + std::to_string(report.numSamplers) + "\n";
                if (!report.envUniforms.empty()) {
                    m_LastCompileLog += "  Env uniforms: ";
                    for (size_t i = 0; i < report.envUniforms.size(); ++i) {
                        m_LastCompileLog += report.envUniforms[i];
                        if (i + 1 < report.envUniforms.size()) m_LastCompileLog += ", ";
                    }
                    m_LastCompileLog += "\n";
                }
                for (const auto& w : report.warnings) {
                    m_LastCompileLog += "Warning: " + w + "\n";
                }
            }

            materialEditor.ApplyPreviewShaderSource(src);

            m_LastShaderSource = std::move(src);
        }
    }

    if (ImGui::Button("Show Generated Shader", ImVec2(-1, 0))) {
        m_ShowShaderSource = !m_ShowShaderSource;
    }

    if (!m_LastCompileLog.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Compile Log:");
        ImGui::BeginChild("compile_log", ImVec2(-1, 120), true);
        ImGui::TextUnformatted(m_LastCompileLog.c_str());
        ImGui::EndChild();
    }

    if (m_ShowShaderSource && !m_LastShaderSource.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Generated Shader Source:");
        ImGui::BeginChild("shader_src", ImVec2(-1, 220), true);
        ImGui::TextUnformatted(m_LastShaderSource.c_str());
        ImGui::EndChild();
    }

    ImGui::End();
}