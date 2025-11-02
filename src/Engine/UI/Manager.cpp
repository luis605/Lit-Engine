module;

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glad/glad.h>
#include <map>
#include <memory>

module Engine.UI.manager;

import Engine.glm;
import Engine.shader;
import Engine.Log;

UIManager::UIManager() {}

UIManager::~UIManager() {}

void UIManager::init(const int windowWidth, const int windowHeight) {
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) {
        Lit::Log::Error("FREETYPE: Could not init FreeType Library");
        return;
    }

    FT_Face face;
    if (FT_New_Face(ft, "resources/fonts/Inter.ttf", 0, &face)) {
        Lit::Log::Error("FREETYPE: Failed to load font");
        return;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (unsigned char c = 0; c < 128; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) {
            Lit::Log::Warn("FREETYPE: Failed to load Glyph");
            continue;
        }

        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RED,
            face->glyph->bitmap.width,
            face->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face->glyph->bitmap.buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            texture,
            glm::ivec2(face->glyph->bitmap.width, face->glyph->bitmap.rows),
            glm::ivec2(face->glyph->bitmap_left, face->glyph->bitmap_top),
            static_cast<unsigned int>(face->glyph->advance.x)};
        m_characters.insert(std::pair<char, Character>(c, character));
    }

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_shader = std::make_unique<Shader>("resources/shaders/text.vert", "resources/shaders/text.frag");
}

void UIManager::cleanup() {
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    for (auto const& [key, val] : m_characters) {
        glDeleteTextures(1, &val.textureID);
    }
}

void UIManager::addText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    m_texts.push_back({text, x, y, scale, color});
}

void UIManager::render() {
    m_shader->bind();
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(m_windowWidth), 0.0f, static_cast<float>(m_windowHeight));
    m_shader->setUniform("projection", projection);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_vao);

    for (const auto& textData : m_texts) {
        m_shader->setUniform("textColor", textData.color);
        glActiveTexture(GL_TEXTURE0);

        float x = textData.x;
        std::string::const_iterator c;
        for (c = textData.text.begin(); c != textData.text.end(); c++) {
            Character ch = m_characters[*c];

            float xpos = x + ch.bearing.x * textData.scale;
            float ypos = textData.y - (ch.size.y - ch.bearing.y) * textData.scale;

            float w = ch.size.x * textData.scale;
            float h = ch.size.y * textData.scale;

            float vertices[6][4] = {
                {xpos, ypos + h, 0.0f, 0.0f},
                {xpos, ypos, 0.0f, 1.0f},
                {xpos + w, ypos, 1.0f, 1.0f},

                {xpos, ypos + h, 0.0f, 0.0f},
                {xpos + w, ypos, 1.0f, 1.0f},
                {xpos + w, ypos + h, 1.0f, 0.0f}};

            glBindTexture(GL_TEXTURE_2D, ch.textureID);
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            x += (ch.advance >> 6) * textData.scale;
        }
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    m_texts.clear();
}
