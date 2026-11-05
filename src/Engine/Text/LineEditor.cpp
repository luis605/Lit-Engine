module;

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

module Engine.LineEditor;

namespace {
constexpr unsigned char kContinuationMask = 0xC0;
constexpr unsigned char kContinuationPattern = 0x80;
constexpr unsigned char kFirstPrintable = 32;
constexpr unsigned char kDelete = 127;

bool isContinuation(unsigned char byte) { return (byte & kContinuationMask) == kContinuationPattern; }
bool isWordChar(unsigned char byte) { return std::isalnum(byte) || byte == '_' || byte >= kContinuationPattern; }
}

void LineEditor::begin(std::string initial, size_t maxLength) {
    m_active = true;
    m_maxLength = maxLength;
    m_original = initial;
    m_text = std::move(initial);
    if (m_text.size() > m_maxLength) {
        size_t cut = m_maxLength;
        while (cut > 0 && isContinuation(static_cast<unsigned char>(m_text[cut]))) --cut;
        m_text.resize(cut);
    }
    m_cursor = m_text.size();
}

void LineEditor::cancel() {
    m_active = false;
    m_text = m_original;
    m_cursor = m_text.size();
}

std::string LineEditor::display() const {
    std::string shown = m_text;
    if (m_active) shown.insert(m_cursor, "|");
    return shown;
}

size_t LineEditor::previousBoundary(size_t from) const {
    if (from == 0) return 0;
    size_t position = from - 1;
    while (position > 0 && isContinuation(static_cast<unsigned char>(m_text[position]))) --position;
    return position;
}

size_t LineEditor::nextBoundary(size_t from) const {
    if (from >= m_text.size()) return m_text.size();
    size_t position = from + 1;
    while (position < m_text.size() && isContinuation(static_cast<unsigned char>(m_text[position]))) ++position;
    return position;
}

EditResult LineEditor::insert(std::string_view utf8) {
    if (!m_active || utf8.empty()) return EditResult::None;
    std::string filtered;
    filtered.reserve(utf8.size());
    for (const char character : utf8) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= kFirstPrintable && byte != kDelete) filtered.push_back(character);
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
            size_t position = m_cursor;
            while (position > 0 && !isWordChar(static_cast<unsigned char>(m_text[position - 1]))) --position;
            while (position > 0 && isWordChar(static_cast<unsigned char>(m_text[position - 1]))) --position;
            m_cursor = position;
            return EditResult::None;
        }
        case EditKey::WordRight: {
            size_t position = m_cursor;
            while (position < m_text.size() && !isWordChar(static_cast<unsigned char>(m_text[position]))) ++position;
            while (position < m_text.size() && isWordChar(static_cast<unsigned char>(m_text[position]))) ++position;
            m_cursor = position;
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
            const size_t boundary = nextBoundary(m_cursor);
            m_text.erase(m_cursor, boundary - m_cursor);
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
