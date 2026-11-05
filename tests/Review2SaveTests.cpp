#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Json;
import Engine.Rng;
import Engine.Save;
import Engine.Delta;

namespace {

namespace fs = std::filesystem;

constexpr int kEntityCount = 40;
constexpr int kSaveRounds = 30;

struct Dir {
    fs::path path;
    Dir() {
        path = fs::temp_directory_path() / "litreview2_save";
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Dir() { fs::remove_all(path); }
};

void populate(World& world) {
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    for (int index = 0; index < kEntityCount; ++index) world.create("e" + std::to_string(index), 1, {static_cast<float>(index), 0.0f, 0.0f});
}

std::string readText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void writeText(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

std::string hex(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string forgedFile(const Json& body, const std::string& version) {
    const std::string bodyText = body.dump();
    return "{\"format\":\"litsave\",\"version\":" + version + ",\"checksum\":\"" + hex(hashName(bodyText)) + "\",\"body\":" + bodyText + "}";
}

void testTruncatedAndCorruptFiles() {
    Dir dir;
    World world;
    populate(world);
    SaveSystem save(SaveContext{&world});
    const fs::path path = dir.path / "a.litsave";
    CHECK(save.save(path.string()));
    const std::string good = readText(path);
    CHECK(save.load(path.string()));
    const fs::path broken = dir.path / "broken.litsave";
    for (size_t cut = 0; cut < good.size(); cut += 1 + good.size() / 120) {
        writeText(broken, good.substr(0, cut));
        std::string error;
        CHECK(!save.load(broken.string(), &error));
    }
    for (size_t offset = 0; offset < good.size(); offset += 1 + good.size() / 150) {
        std::string flipped = good;
        flipped[offset] = static_cast<char>(flipped[offset] ^ 0x21);
        writeText(broken, flipped);
        save.load(broken.string());
    }
    CHECK(!save.load((dir.path / "missing.litsave").string()));
    CHECK(!save.load(dir.path.string()));
}

void testForgedChecksumFields() {
    Dir dir;
    World world;
    populate(world);
    SaveSystem save(SaveContext{&world});
    const Json body = save.capture();
    const fs::path path = dir.path / "forged.litsave";
    for (const char* version : {"1e30", "-1e30", "1.5", "0", "2", "-3", "1e999"}) {
        writeText(path, forgedFile(body, version));
        const bool loaded = save.load(path.string());
        CHECK(loaded == (std::string(version) == "1.5"));
    }
    Json hugeSlots = body;
    hugeSlots.set("slots", 4.0e9);
    writeText(path, forgedFile(hugeSlots, "1"));
    std::string error;
    CHECK(!save.load(path.string(), &error));
    Json negative = body;
    negative.set("slots", -5.0);
    Json meta = Json::object();
    meta.set("serial", -7.0);
    negative.set("meta", meta);
    writeText(path, forgedFile(negative, "1"));
    save.load(path.string());
    Json camera = Json::object();
    Json fakePosition = Json::object();
    fakePosition.set("a", 1.0);
    fakePosition.set("b", 2.0);
    fakePosition.set("c", 3.0);
    camera.set("position", fakePosition);
    Json shaped = body;
    shaped.set("camera", camera);
    writeText(path, forgedFile(shaped, "1"));
    save.load(path.string());
    writeText(path, "{\"format\":\"litsave\",\"version\":1,\"checksum\":5,\"body\":{}}");
    CHECK(!save.load(path.string()));
}

void testAsyncLifecycle() {
    Dir dir;
    const fs::path path = dir.path / "async.litsave";
    {
        World world;
        populate(world);
        SaveSystem save(SaveContext{&world});
        for (int round = 0; round < kSaveRounds; ++round) CHECK(save.saveAsync(path.string(), "r"));
        CHECK(save.waitPending());
    }
    {
        World world;
        populate(world);
        SaveSystem save(SaveContext{&world});
        CHECK(save.saveAsync(path.string()));
    }
    World world;
    populate(world);
    SaveSystem loader(SaveContext{&world});
    CHECK(loader.load(path.string()));
    {
        SaveSystem failing(SaveContext{&world});
        CHECK(failing.saveAsync("/proc/litreview2/none.litsave"));
        CHECK(!failing.waitPending());
        CHECK(failing.saveAsync("/proc/litreview2/none.litsave"));
    }
}

void testConcurrentSaveAndLoad() {
    Dir dir;
    const fs::path path = dir.path / "shared.litsave";
    World worldA;
    populate(worldA);
    SaveSystem seed(SaveContext{&worldA});
    CHECK(seed.save(path.string()));
    std::atomic<bool> stop{false};
    std::atomic<int> badLoads{0};
    std::vector<std::thread> writers;
    for (int writer = 0; writer < 3; ++writer) {
        writers.emplace_back([&] {
            World world;
            populate(world);
            SaveSystem save(SaveContext{&world});
            for (int round = 0; round < kSaveRounds; ++round) save.save(path.string());
        });
    }
    std::thread reader([&] {
        World world;
        populate(world);
        SaveSystem save(SaveContext{&world});
        while (!stop) {
            std::string error;
            if (!save.load(path.string(), &error) && error.find("cannot open") == std::string::npos) ++badLoads;
        }
    });
    for (std::thread& writer : writers) writer.join();
    stop = true;
    reader.join();
    CHECK(badLoads == 0);
    int leftovers = 0;
    for (const auto& entry : fs::directory_iterator(dir.path)) leftovers += entry.path().extension() == ".tmp" ? 1 : 0;
    CHECK(leftovers == 0);
}

void testMalformedDelta() {
    World world;
    populate(world);
    DeltaStore store(world);
    const char* documents[] = {
        "{\"cells\":{\"c\":{\"changed\":{\"k\":{\"local\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,\"j\":10,\"k\":11,\"l\":12,\"m\":13,\"n\":14,\"o\":15,\"p\":16},\"mesh\":-5,\"layer\":1e30,\"components\":[{\"a\":1,\"b\":2}]}},\"removed\":[1,null,{}],\"added\":{\"x\":[{\"parent\":1e30,\"source\":-1}]},\"params\":{\"gravity\":{\"x\":1,\"y\":2,\"z\":3}}}},\"serial\":-1}",
        "{\"cells\":{\"c\":5}}",
        "{\"cells\":{\"c\":{\"changed\":[1,2],\"added\":[3],\"removed\":{}}},\"serial\":1e300}",
        "{\"cells\":[]}",
    };
    for (const char* text : documents) {
        auto parsed = Json::parse(text);
        CHECK(parsed.has_value());
        if (parsed) {
            store.fromJson(*parsed);
            store.applyCell("c");
        }
    }
    CHECK(store.toJson().isObject());
}

}

void runReview2SaveTests() {
    testTruncatedAndCorruptFiles();
    testForgedChecksumFields();
    testAsyncLifecycle();
    testConcurrentSaveAndLoad();
    testMalformedDelta();
    runReview2SaveRollbackTests();
}
