#pragma once

#include <cstdint>
#include <set>
#include <vector>

// Slot allocator of the bindless texture table (docs/RenderGraph.md, Bindless). No GPU types, so it is unit tested headless
// (tests/TextureTableTests.cpp). Slots are stable for the lifetime of a texture: the lowest free slot is reused first, a released
// slot may be handed out again, and nothing moves.
namespace texslots {

class SlotAllocator {
  public:
    explicit SlotAllocator(uint32_t capacity = 0) : m_capacity(capacity) {}

    // Lowest free slot, or -1 when the table is full.
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

    // False for a slot that is not allocated (double release, out of range, negative).
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
    // One past the highest slot in use: the part of the table a shader can reach.
    [[nodiscard]] uint32_t highWater() const { return m_next; }
    [[nodiscard]] bool full() const { return m_used >= m_capacity; }

    void clear() {
        m_free.clear();
        m_next = 0;
        m_used = 0;
    }

  private:
    uint32_t m_capacity = 0;
    uint32_t m_next = 0;  // slots [0, m_next) have been handed out at some point; m_free are the released ones among them
    uint32_t m_used = 0;
    std::set<uint32_t> m_free;
};

}
