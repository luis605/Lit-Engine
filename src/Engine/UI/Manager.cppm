module;

#include <vector>
#include <memory>
#include <string>
#include <map>
#include <memory>

import Engine.glm;
import Engine.shader;

export module Engine.UI.manager;

export class UIManager {
  public:
    UIManager();
    ~UIManager();

    void init(const int windowWidth, const int windowHeight);
    void cleanup();

    void addText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void render();

  private:
    struct Character {
        unsigned int textureID;
        glm::ivec2 size;
        glm::ivec2 bearing;
        unsigned int advance;
    };

    std::map<char, Character> m_characters;
    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_windowWidth;
    unsigned int m_windowHeight;

    std::unique_ptr<Shader> m_shader;

    struct TextData {
        std::string text;
        float x, y, scale;
        glm::vec3 color;
    };
    std::vector<TextData> m_texts;
};
