#include "TestSupport.hpp"
#include "EdgeSuites.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

import Engine.Events;
import Engine.Selection;
import Engine.World;
import Engine.Render.entity;

namespace {

struct Ping {
    int value;
};

struct Pong {
    int value;
};

void testBusBasics() {
    EventBus bus;
    CHECK(!bus.hasSubscribers<Ping>());
    bus.emit(Ping{1});
    CHECK(bus.pending() == 0);
    bus.emitNow(Ping{1});
    bus.dispatch();

    int sum = 0;
    const uint32_t id = bus.subscribe<Ping>([&](const Ping& ping) { sum += ping.value; });
    CHECK(bus.hasSubscribers<Ping>() && !bus.hasSubscribers<Pong>());
    bus.emit(Ping{2});
    CHECK(bus.pending() == 1 && sum == 0);
    bus.dispatch();
    CHECK(sum == 2 && bus.pending() == 0);
    bus.unsubscribe(id);
    bus.unsubscribe(id);
    bus.unsubscribe(9999);
    CHECK(!bus.hasSubscribers<Ping>());
    bus.emitNow(Ping{5});
    CHECK(sum == 2);
}

void testQueuedEventAfterUnsubscribe() {
    EventBus bus;
    int calls = 0;
    const uint32_t id = bus.subscribe<Ping>([&](const Ping&) { ++calls; });
    bus.emit(Ping{1});
    bus.unsubscribe(id);
    bus.dispatch();
    CHECK(calls == 0);
}

void testReentrancy() {
    EventBus bus;
    std::vector<int> order;
    uint32_t selfRemoving = 0;
    selfRemoving = bus.subscribe<Ping>([&](const Ping& ping) {
        order.push_back(ping.value);
        bus.unsubscribe(selfRemoving);
    });
    int lateCalls = 0;
    bus.subscribe<Ping>([&](const Ping&) {
        bus.subscribe<Ping>([&](const Ping&) { ++lateCalls; });
    });
    bus.emitNow(Ping{1});
    CHECK(order.size() == 1 && lateCalls == 0);
    bus.emitNow(Ping{2});
    CHECK(order.size() == 1 && lateCalls == 1);

    EventBus chain;
    std::vector<int> seen;
    chain.subscribe<Ping>([&](const Ping& ping) {
        seen.push_back(ping.value);
        if (ping.value < 3) chain.emit(Ping{ping.value + 1});
    });
    chain.emit(Ping{0});
    chain.dispatch();
    CHECK(seen == (std::vector<int>{0, 1, 2, 3}));
    CHECK(chain.pending() == 0);
}

void testSelectionEdges() {
    World world;
    const EntityHandle first = world.create("first", 1);
    const EntityHandle second = world.create("second", 1);
    const EntityHandle child = world.create("child", 1);
    world.setParent(child, first);

    Selection selection;
    CHECK(selection.empty() && selection.primary().isNull());
    selection.add(NULL_ENTITY);
    selection.set(NULL_ENTITY);
    CHECK(selection.empty());
    selection.remove(first);
    selection.toggle(first);
    selection.toggle(first);
    CHECK(selection.empty());

    selection.add(first);
    selection.add(first);
    selection.add(second);
    CHECK(selection.size() == 2 && selection.primary() == second);
    selection.remove(second);
    CHECK(selection.primary() == first);

    selection.setAll({first, second, first, NULL_ENTITY, child});
    CHECK(selection.size() == 3);
    selection.removeDescendantsOfSelected(world);
    CHECK(selection.size() == 2 && !selection.contains(child));

    world.destroy(second);
    selection.prune(world);
    CHECK(selection.size() == 1 && selection.contains(first) && !selection.contains(second));

    const EntityHandle recycled = world.create("recycled", 1);
    CHECK(!selection.contains(recycled));
    selection.clear();
    CHECK(selection.empty() && !selection.contains(first));
}

}

void runEdgeEventsSelectionTests() {
    testBusBasics();
    testQueuedEventAfterUnsubscribe();
    testReentrancy();
    testSelectionEdges();
}
