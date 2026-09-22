module;

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

module Engine.LineEditor;

namespace {
bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }
bool isWordChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }
}

void LineEditor::begin(std::string initial, size_t maxLength) {
    m_active = true;
    m_maxLength = maxLength;
    m_original = initial;
    m_text = std::move(initial);
    if (m_text.size() > m_maxLength) m_text.resize(m_maxLength);
    m_cursor = m_text.size();
}

void LineEditor::cancel() {
    m_active = false;
    m_text = m_original;
    m_cursor = m_text.size();
}

std::string LineEditor::display() const {
    std::string out = m_text;
    out.insert(m_cursor, m_active ? "|" : "");
    return out;
}

size_t LineEditor::previousBoundary(size_t from) const {
    if (from == 0) return 0;
    size_t i = from - 1;
    while (i > 0 && isContinuation(static_cast<unsigned char>(m_text[i]))) --i;
    return i;
}

size_t LineEditor::nextBoundary(size_t from) const {
    if (from >= m_text.size()) return m_text.size();
    size_t i = from + 1;
    while (i < m_text.size() && isContinuation(static_cast<unsigned char>(m_text[i]))) ++i;
    return i;
}

EditResult LineEditor::insert(std::string_view utf8) {
    if (!m_active || utf8.empty()) return EditResult::None;
    std::string filtered;
    for (char c : utf8) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 32 && u != 127) filtered.push_back(c);
    }
    if (filtered.empty() || m_text.size() + filtered.size() > m_maxLength) return EditResult::None;
    m_text.insert(m_cursor, filtered);
    m_cursor += filtered.size();
    return EditResult::Changed;
}

EditResult LineEditor::press(EditKey key) {
    if (!m_active) return EditResult::None;
    switch (key) {
        case EditKey::Left:
            m_cursor = previousBoundary(m_cursor);
            return EditResult::None;
        case EditKey::Right:
            m_cursor = nextBoundary(m_cursor);
            return EditResult::None;
        case EditKey::Home:
            m_cursor = 0;
            return EditResult::None;
        case EditKey::End:
            m_cursor = m_text.size();
            return EditResult::None;
        case EditKey::WordLeft: {
            size_t i = m_cursor;
            while (i > 0 && !isWordChar(static_cast<unsigned char>(m_text[i - 1]))) --i;
            while (i > 0 && isWordChar(static_cast<unsigned char>(m_text[i - 1]))) --i;
            m_cursor = i;
            return EditResult::None;
        }
        case EditKey::WordRight: {
            size_t i = m_cursor;
            while (i < m_text.size() && !isWordChar(static_cast<unsigned char>(m_text[i]))) ++i;
            while (i < m_text.size() && isWordChar(static_cast<unsigned char>(m_text[i]))) ++i;
            m_cursor = i;
            return EditResult::None;
        }
        case EditKey::Backspace: {
            if (m_cursor == 0) return EditResult::None;
            const size_t start = previousBoundary(m_cursor);
            m_text.erase(start, m_cursor - start);
            m_cursor = start;
            return EditResult::Changed;
        }
        case EditKey::Delete: {
            if (m_cursor >= m_text.size()) return EditResult::None;
            const size_t end = nextBoundary(m_cursor);
            m_text.erase(m_cursor, end - m_cursor);
            return EditResult::Changed;
        }
        case EditKey::Clear:
            if (m_text.empty()) return EditResult::None;
            m_text.clear();
            m_cursor = 0;
            return EditResult::Changed;
        case EditKey::Enter:
            m_active = false;
            return EditResult::Committed;
        case EditKey::Escape:
            cancel();
            return EditResult::Cancelled;
    }
    return EditResult::None;
}
