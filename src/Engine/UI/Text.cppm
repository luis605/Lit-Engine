module;

#include <string>

import Engine.glm;

export module Engine.UI.text;

export struct Text {
    std::string content;
    glm::vec2 position;
    float scale;
    glm::vec3 color;
};
