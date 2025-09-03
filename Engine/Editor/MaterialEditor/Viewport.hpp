/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_VIEWPORT_HPP
#define MATERIAL_VIEWPORT_HPP

#include <raylib.h>
#include <vector>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Core/Entity.hpp>
#include <optional>
#include <string>

class ViewportPanel {
private:
    RenderTexture            m_MaterialRT;
    LitCamera                m_MaterialCamera;
    std::optional<Entity>    m_Entity;
    std::vector<LightStruct> m_Lights;

    float m_EnvHumidity = 1.0f;

public:
    ViewportPanel();
    ~ViewportPanel();

    void Init();
    void Render();

    void ApplyPreviewFragmentShader(const std::string& fragSource);
};

#endif // MATERIAL_VIEWPORT_HPP