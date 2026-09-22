module;

#include <cstddef>
#include <string>
#include <string_view>

export module Engine.LineEditor;

export enum class EditKey { Left, Right, Home, End, Backspace, Delete, Enter, Escape, WordLeft, WordRight, Clear };

export enum class EditResult { None, Changed, Committed, Cancelled };

export class LineEditor {
  public:
    void begin(std::string initial = {}, size_t maxLength = 256);
    void cancel();
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] const std::string& text() const { return m_text; }
    [[nodiscard]] size_t cursor() const { return m_cursor; }
    [[nodiscard]] std::string display() const;

    EditResult insert(std::string_view utf8);
    EditResult press(EditKey key);

  private:
    [[nodiscard]] size_t previousBoundary(size_t from) const;
    [[nodiscard]] size_t nextBoundary(size_t from) const;

    bool m_active = false;
    std::string m_text;
    std::string m_original;
    size_t m_cursor = 0;
    size_t m_maxLength = 256;
};
