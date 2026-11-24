#include "TestSupport.hpp"
#include "Engine/Render/TextureSlots.hpp"

#include <set>
#include <vector>

// Slot allocator of the bindless texture table (docs/RenderGraph.md). The GPU side (bindless table vs legacy arrays render the same
// pixels) is the BindlessTextureParity test, scripts/bindless_check.sh.

namespace {

void testAllocateInOrderAndReuse() {
    texslots::SlotAllocator slots(8);
    CHECK(slots.capacity() == 8);
    CHECK(slots.used() == 0);
    CHECK(slots.highWater() == 0);
    for (int expected = 0; expected < 5; ++expected) CHECK(slots.allocate() == expected);
    CHECK(slots.used() == 5);
    CHECK(slots.highWater() == 5);
    // A released slot below the high-water mark is the next one handed out (lowest free first) and nothing else moves.
    CHECK(slots.release(1));
    CHECK(slots.release(3));
    CHECK(slots.used() == 3);
    CHECK(slots.highWater() == 5);
    CHECK(!slots.allocated(1));
    CHECK(slots.allocated(2));
    CHECK(slots.allocate() == 1);
    CHECK(slots.allocate() == 3);
    CHECK(slots.allocate() == 5);
    CHECK(slots.highWater() == 6);
}

void testExhaustion() {
    texslots::SlotAllocator slots(4);
    std::set<int> handed;
    for (int i = 0; i < 4; ++i) handed.insert(slots.allocate());
    CHECK(handed.size() == 4);
    CHECK(slots.full());
    CHECK(slots.allocate() == -1);
    CHECK(slots.allocate() == -1);
    CHECK(slots.used() == 4);
    CHECK(slots.release(2));
    CHECK(!slots.full());
    CHECK(slots.allocate() == 2);
    CHECK(slots.allocate() == -1);
    texslots::SlotAllocator empty(0);
    CHECK(empty.allocate() == -1);
}

void testReleaseValidation() {
    texslots::SlotAllocator slots(4);
    CHECK(!slots.release(0));
    CHECK(!slots.release(-1));
    CHECK(!slots.release(99));
    CHECK(slots.allocate() == 0);
    CHECK(slots.release(0));
    CHECK(!slots.release(0));
    CHECK(slots.used() == 0);
    CHECK(slots.highWater() == 0);
    CHECK(slots.allocate() == 0);
}

void testHighWaterShrinksPastTrailingFreeSlots() {
    texslots::SlotAllocator slots(16);
    for (int i = 0; i < 6; ++i) slots.allocate();
    CHECK(slots.release(3));
    CHECK(slots.release(4));
    CHECK(slots.highWater() == 6);
    CHECK(slots.release(5));
    // 5, 4 and 3 are all free now: the reachable part of the table ends after slot 2.
    CHECK(slots.highWater() == 3);
    CHECK(slots.used() == 3);
    CHECK(slots.allocate() == 3);
}

void testStableUnderChurn() {
    texslots::SlotAllocator slots(64);
    std::vector<int> live;
    unsigned seed = 12345;
    for (int step = 0; step < 4000; ++step) {
        seed = seed * 1664525u + 1013904223u;
        if ((seed >> 16) % 3 != 0 || live.empty()) {
            const int slot = slots.allocate();
            if (slot < 0) {
                CHECK(slots.used() == 64);
                continue;
            }
            CHECK(slot < 64);
            for (int other : live) CHECK(other != slot);
            live.push_back(slot);
        } else {
            const size_t pick = (seed >> 8) % live.size();
            CHECK(slots.release(live[pick]));
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(pick));
        }
        CHECK(slots.used() == live.size());
    }
    slots.clear();
    CHECK(slots.used() == 0);
    CHECK(slots.allocate() == 0);
}

}

int main() {
    testAllocateInOrderAndReuse();
    testExhaustion();
    testReleaseValidation();
    testHighWaterShrinksPastTrailingFreeSlots();
    testStableUnderChurn();
    return testsupport::finish();
}
