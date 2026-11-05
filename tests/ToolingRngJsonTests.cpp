#include "TestSupport.hpp"
#include "ToolingSuites.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;
import Engine.Rng;
import Engine.Json;
import Engine.Dump;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Render.settings;
import Engine.TestScenes;

static void testRng() {
    Pcg32 ref(42, 54);
    const uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (uint32_t e : expected) CHECK(ref.next() == e);

    Pcg32 a(7), b(7), c(8);
    bool differs = false;
    for (int i = 0; i < 64; ++i) {
        const uint32_t x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);

    Pcg32 r(3);
    for (int i = 0; i < 2000; ++i) {
        const float u = r.uniform();
        CHECK(u >= 0.0f && u < 1.0f);
        CHECK(r.below(10) < 10);
        const int n = r.rangeInt(-3, 3);
        CHECK(n >= -3 && n <= 3);
    }

    Pcg32 saved(11);
    saved.next();
    Pcg32 copy;
    copy.restore(saved.state(), saved.increment());
    CHECK(copy == saved);
    CHECK(copy.next() == saved.next());

    RandomService svc;
    svc.seed(99);
    const uint32_t first = svc.stream("alpha").next();
    const uint32_t other = svc.stream("beta").next();
    CHECK(first != other);
    svc.seed(99);
    CHECK(svc.stream("alpha").next() == first);
    RandomService svc2;
    svc2.seed(99);
    CHECK(svc2.stream("alpha").next() == first);
    svc2.seed(100);
    CHECK(svc2.stream("alpha").next() != first);
}

static void testJson() {
    std::string err;
    auto doc = Json::parse(R"({"a":[1,2.5,{"n":"x\ny"}],"b":true,"c":null,"d":"é","e":-1e3})", &err);
    CHECK(doc.has_value());
    CHECK(doc->get("b")->asBool());
    CHECK(doc->get("c")->isNull());
    CHECK(doc->get("e")->asNumber() == -1000.0);
    CHECK(doc->get("a")->size() == 3);
    CHECK(jsonFind(*doc, "a[2].n")->asString() == "x\ny");
    CHECK(jsonFind(*doc, "a[1]")->asNumber() == 2.5);
    CHECK(!jsonFind(*doc, "a[9]"));
    CHECK(!jsonFind(*doc, "zzz.y"));
    const auto round = Json::parse(doc->dump(), &err);
    CHECK(round.has_value() && *round == *doc);
    const auto pretty = Json::parse(doc->dump(2), &err);
    CHECK(pretty.has_value() && *pretty == *doc);

    CHECK(!Json::parse("{", &err).has_value() && !err.empty());
    CHECK(!Json::parse("[1,]").has_value());
    CHECK(!Json::parse("{\"a\":1} x").has_value());
    CHECK(!Json::parse("").has_value());
    CHECK(!Json::parse(std::string(500, '[')).has_value());

    Json built = Json::object();
    built.set("list", Json::array());
    built.set("v", 3);
    built.set("v", 4);
    CHECK(built.get("v")->asNumber() == 4.0);
    CHECK(built.members().size() == 2);

    auto items = Json::parse(R"({"items":[{"name":"a","v":1},{"name":"b","v":2,"pos":[1,2,3]}]})");
    CHECK_OK(expectNumber(*items, "items[name=b].v", 2.0));
    CHECK_OK(expectVec(*items, "items[name=b].pos", {1.0, 2.0, 3.0}));
    CHECK_OK(expectSize(*items, "items", 2));
    CHECK_OK(expectString(*items, "items[0].name", "a"));
    CHECK(!expectNumber(*items, "items[name=b].v", 3.0).empty());
    CHECK(!expectNumber(*items, "items[name=zz].v", 3.0).empty());
    CHECK(!expectVec(*items, "items[name=b].pos", {1.0, 2.0, 4.0}).empty());
    CHECK(!expectBool(*items, "items[0].name", true).empty());

    auto other = Json::parse(R"({"items":[{"name":"a","v":1},{"name":"b","v":2.5,"pos":[1,2,3]}]})");
    CHECK(jsonDiff(*items, *items).empty());
    CHECK(jsonDiff(*items, *other).find("items[1].v") != std::string::npos);
    CHECK(jsonDiff(*items, *other, 1.0).empty());
}

void runRngJsonTests() {
    testRng();
    testJson();
}
