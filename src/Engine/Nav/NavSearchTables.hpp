#pragma once

inline constexpr size_t kInitialTableSize = 1024;
inline constexpr size_t kProbeLimit = 2048;
inline constexpr uint64_t kHashMultiplier = 0x9E3779B97F4A7C15ULL;

struct NavVisited {
    struct Slot {
        uint64_t key = 0;
        uint32_t stamp = 0;
    };
    std::vector<Slot> slots;
    std::vector<uint64_t> queue;
    uint32_t stamp = 0;

    void reset() {
        if (slots.empty()) slots.resize(kProbeLimit * 2);
        ++stamp;
        queue.clear();
    }

    bool insert(uint64_t key) {
        const size_t mask = slots.size() - 1;
        size_t index = static_cast<size_t>((key * kHashMultiplier) >> 20) & mask;
        while (slots[index].stamp == stamp) {
            if (slots[index].key == key) return false;
            index = (index + 1) & mask;
        }
        slots[index] = {key, stamp};
        return true;
    }
};

struct NavSearchTables {
    NavVisited visited;
    struct Record {
        float cost = 0.0f;
        uint64_t parent = 0;
        int link = -1;
        bool fromA = true;
        bool closed = false;
    };
    struct Slot {
        uint64_t key = 0;
        uint32_t stamp = 0;
        Record record;
    };
    struct Item {
        float estimate;
        float cost;
        uint64_t key;
        bool operator<(const Item& other) const {
            if (estimate != other.estimate) return estimate > other.estimate;
            if (cost != other.cost) return cost < other.cost;
            return key > other.key;
        }
    };

    std::vector<Slot> slots;
    std::vector<Item> heap;
    std::vector<uint64_t> chain;
    std::vector<NavPathPoint> raw;
    std::vector<NavPathPoint> smoothed;
    uint32_t stamp = 0;
    size_t used = 0;

    void reset() {
        if (slots.empty()) slots.resize(kInitialTableSize);
        ++stamp;
        used = 0;
        heap.clear();
    }

    size_t slotIndex(uint64_t key, const std::vector<Slot>& table) const {
        const size_t mask = table.size() - 1;
        size_t index = static_cast<size_t>((key * kHashMultiplier) >> 20) & mask;
        while (table[index].stamp == stamp && table[index].key != key) index = (index + 1) & mask;
        return index;
    }

    Record* find(uint64_t key) {
        Slot& slot = slots[slotIndex(key, slots)];
        return slot.stamp == stamp ? &slot.record : nullptr;
    }

    void grow() {
        std::vector<Slot> bigger(slots.size() * 2);
        std::vector<Slot> old = std::move(slots);
        slots = std::move(bigger);
        for (const Slot& slot : old) {
            if (slot.stamp != stamp) continue;
            slots[slotIndex(slot.key, slots)] = slot;
        }
    }

    Record& insert(uint64_t key) {
        if ((used + 1) * 2 > slots.size()) grow();
        Slot& slot = slots[slotIndex(key, slots)];
        if (slot.stamp != stamp) {
            slot.stamp = stamp;
            slot.key = key;
            ++used;
        }
        return slot.record;
    }
};
