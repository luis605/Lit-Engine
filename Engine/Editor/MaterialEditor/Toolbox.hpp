/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef MATERIAL_TOOLBOX_HPP
#define MATERIAL_TOOLBOX_HPP

#include <string>

class ToolboxPanel {
public:
    ToolboxPanel();
    ~ToolboxPanel();

    void Render();

private:
    std::string m_LastCompileLog;
    std::string m_LastShaderSource;
    bool m_ShowShaderSource = false;
};

#endif // MATERIAL_TOOLBOX_HPP