#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace texslots {

class SlotAllocator {
  public:
    explicit SlotAllocator(uint32_t capacity = 0) : m_capacity(capacity) {}

    int allocate() {
        if (!m_free.empty()) {
            const uint32_t slot = *m_free.begin();
            m_free.erase(m_free.begin());
            ++m_used;
            return static_cast<int>(slot);
        }
        if (m_next >= m_capacity) return -1;
        ++m_used;
        return static_cast<int>(m_next++);
    }

    bool release(int slot) {
        if (!allocated(slot)) return false;
        const auto index = static_cast<uint32_t>(slot);
        if (index + 1 == m_next) {
            --m_next;
            while (m_next > 0 && m_free.erase(m_next - 1) != 0) --m_next;
        } else {
            m_free.insert(index);
        }
        --m_used;
        return true;
    }

    [[nodiscard]] bool allocated(int slot) const { return slot >= 0 && static_cast<uint32_t>(slot) < m_next && !m_free.contains(static_cast<uint32_t>(slot)); }
    [[nodiscard]] uint32_t capacity() const { return m_capacity; }
    [[nodiscard]] uint32_t used() const { return m_used; }
    [[nodiscard]] uint32_t highWater() const { return m_next; }
    [[nodiscard]] bool full() const { return m_used >= m_capacity; }

    void clear() {
        m_free.clear();
        m_next = 0;
        m_used = 0;
    }

  private:
    uint32_t m_capacity = 0;
    uint32_t m_next = 0;
    uint32_t m_used = 0;
    std::set<uint32_t> m_free;
};

}
