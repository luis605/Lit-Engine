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
import Engine.Portal;
import Engine.Portal.view;
import Engine.PortalBridge;
import Engine.Streaming;
import Engine.Delta;
import Engine.Save;
import Engine.Nav;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;
import Engine.Json;

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    }
}
}  // namespace

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

constexpr float kHalfWidth = 1.4f;
constexpr float kHalfHeight = 1.8f;
constexpr float kSpacing = 24.0f;

static std::string tempPath(const std::string& name) { return (std::filesystem::temp_directory_path() / ("lit_streaming_" + name)).string(); }

static void buildLine(PortalSystem& p, int count) {
    for (int i = 0; i < count; ++i) {
        PortalCell c;
        c.name = "C" + std::to_string(i);
        c.min = glm::vec3(kSpacing * static_cast<float>(i) - 5.4f, -0.4f, -5.4f);
        c.max = glm::vec3(kSpacing * static_cast<float>(i) + 5.4f, 4.4f, 5.4f);
        p.addCell(c);
    }
    auto end = [](const std::string& cell, float x, float yaw) {
        PortalEnd e;
        e.cell = cell;
        e.position = glm::vec3(x, kHalfHeight, 0.0f);
        e.eulerDegrees = glm::vec3(yaw, 0.0f, 0.0f);
        e.halfSize = glm::vec2(kHalfWidth, kHalfHeight);
        return e;
    };
    for (int i = 0; i + 1 < count; ++i) {
        const float x = kSpacing * static_cast<float>(i);
        p.addPair("P" + std::to_string(i), end("C" + std::to_string(i), x + 5.0f, -90.0f), end("C" + std::to_string(i + 1), x + kSpacing - 5.0f, 90.0f));
    }
}

static CellContent cellContent(int cell, int count, bool slow) {
    if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(4));
    CellContent content;
    for (int j = 0; j < count; ++j) {
        CellEntity e;
        e.key = "k" + std::to_string(j);
        e.desc.name = "c" + std::to_string(cell) + "_" + std::to_string(j);
        e.desc.position = glm::vec3(kSpacing * static_cast<float>(cell) + static_cast<float>(j % 10) * 0.5f - 2.0f, 0.5f, static_cast<float>(j / 10) * 0.5f - 2.0f);
        content.entities.push_back(std::move(e));
    }
    return content;
}

static std::vector<double> column(const std::string& path, const std::string& name) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    std::vector<std::string> names;
    std::stringstream header(line);
    for (std::string cell; std::getline(header, cell, ',');) names.push_back(cell);
    const auto it = std::find(names.begin(), names.end(), name);
    std::vector<double> values;
    if (it == names.end()) return values;
    const size_t index = static_cast<size_t>(it - names.begin());
    while (std::getline(in, line)) {
        std::stringstream row(line);
        std::string cell;
        for (size_t i = 0; std::getline(row, cell, ','); ++i) {
            if (i == index) values.push_back(std::stod(cell));
        }
    }
    return values;
}

static void testStreamingRoute() {
    constexpr int kCells = 12;
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), kCells);
    StreamingConfig config;
    config.loadHops = 1;
    config.keepHops = 2;
    config.commitMillisPerFrame = 0.5;
    config.hitchMillis = 25.0;
    config.window = 4096;
    StreamingSystem& streaming = engine.enableStreaming(config);
    DeltaStore& deltas = engine.enableDeltas();
    streaming.setLoader(makeBatchLoader(engine.world(), engine.portals(), &deltas, [](int cell) { return cellContent(cell, 800, true); }));
    engine.portals().setCameraCell(0);
    streaming.prime(0);
    CHECK(streaming.resident(0) && streaming.resident(1) && !streaming.resident(2));

    const std::string csv = tempPath("route.csv");
    engine.enablePerfLog(csv);
    Camera& camera = engine.camera();
    camera.setPos(glm::vec3(-3.2f, 1.7f, 0.0f));
    camera.setOrientation(0.0f, 0.0f);

    size_t popIns = 0;
    size_t maxResident = 0;
    int lastCell = 0;
    int visited = 1;
    for (int frame = 0; frame < 4000 && visited < kCells; ++frame) {
        camera.setPos(camera.getPosition() + glm::vec3(0.1f, 0.0f, 0.0f));
        engine.portals().step(camera);
        engine.tick(1.0f / 60.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int cell = engine.portals().cameraCell();
        if (cell != lastCell) {
            lastCell = cell;
            ++visited;
        }
        if (!streaming.resident(cell)) ++popIns;
        maxResident = std::max(maxResident, streaming.residentCells().size());
    }
    const StreamingStats stats = streaming.stats();
    engine.disablePerfLog();

    CHECK(visited == kCells);
    CHECK(popIns == 0);
    CHECK(maxResident <= 5);
    CHECK(stats.cellsLoaded >= static_cast<uint64_t>(kCells));
    CHECK(stats.cellsUnloaded >= static_cast<uint64_t>(kCells - 4));
    CHECK(stats.hitches == 0);
    CHECK(stats.frameP99 < 10.0);
    CHECK(stats.updateMaxMillis < 6.0);
    std::printf("route: frames %llu p50 %.2f ms p99 %.2f ms max %.2f ms update max %.2f ms loaded %llu unloaded %llu hitches %llu\n", static_cast<unsigned long long>(stats.frames), stats.frameP50, stats.frameP99, stats.frameMax, stats.updateMaxMillis, static_cast<unsigned long long>(stats.cellsLoaded), static_cast<unsigned long long>(stats.cellsUnloaded), static_cast<unsigned long long>(stats.hitches));

    const std::vector<double> p99 = column(csv, "frame_p99_ms");
    const std::vector<double> resident = column(csv, "stream_resident");
    CHECK(!p99.empty() && !resident.empty());
    CHECK(!p99.empty() && p99.back() < 10.0);
    std::vector<double> cpu = column(csv, "cpu_ms");
    std::sort(cpu.begin(), cpu.end());
    CHECK(!cpu.empty() && cpu[cpu.size() * 99 / 100] < 10.0);
    std::filesystem::remove(csv);
}

static void testBudgetedCommitBounds() {
    Engine engine;
    engine.openHeadless();
    buildLine(engine.portals(), 3);
    StreamingConfig config;
    config.commitMillisPerFrame = 0.2;
    config.uploadBytesPerFrame = 256 * 50;
    StreamingSystem& streaming = engine.enableStreaming(config);
    DeltaStore& deltas = engine.enableDeltas();
    streaming.setLoader(makeBatchLoader(engine.world(), engine.portals(), &deltas, [](int cell) { return cellContent(cell, 2000, false); }));
    engine.portals().setCameraCell(0);
    size_t before = engine.world().aliveCount();
    size_t maxStep = 0;
    int frames = 0;
    while (!(streaming.resident(0) && streaming.resident(1)) && frames < 5000) {
        engine.tick(1.0f / 60.0f);
        const size_t now = engine.world().aliveCount();
        maxStep = std::max(maxStep, now - before);
        before = now;
        ++frames;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    CHECK(streaming.resident(0) && streaming.resident(1));
    CHECK(maxStep <= 51);
    CHECK(frames > 40);
}

static void testAssetStreaming() {
    PortalSystem portals;
    buildLine(portals, 2);
    StreamingConfig config;
    config.uploadBytesPerFrame = 1000;
    StreamingSystem streaming(portals, config);
    int finished = 0;
    for (int i = 0; i < 6; ++i) {
        AssetRequest r;
        r.key = "tex" + std::to_string(i);
        r.bytes = 600;
        r.priority = i;
        r.load = [] { return std::make_shared<int>(7); };
        r.finish = [&finished](std::shared_ptr<void> p) { finished += *static_cast<int*>(p.get()) == 7 ? 1 : 0; };
        streaming.requestAsset(std::move(r));
    }
    streaming.jobs().waitIdle();
    int maxPerFrame = 0;
    for (int frame = 0; frame < 20; ++frame) {
        const int before = finished;
        streaming.update(0);
        maxPerFrame = std::max(maxPerFrame, finished - before);
    }
    CHECK(finished == 6);
    CHECK(maxPerFrame <= 2);
    CHECK(streaming.assetReady("tex5"));
}

struct DeltaRig {
    Engine engine;
    DeltaStore* deltas = nullptr;
    StreamingSystem* streaming = nullptr;
    SaveSystem* save = nullptr;

    DeltaRig() {
        engine.openHeadless();
        buildLine(engine.portals(), 4);
        engine.portals().cellParams(1).gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        deltas = &engine.enableDeltas();
        deltas->snapshotParams();
        StreamingConfig config;
        config.loadHops = 1;
        config.keepHops = 1;
        streaming = &engine.enableStreaming(config);
        streaming->setLoader(makeBatchLoader(engine.world(), engine.portals(), deltas, [](int cell) { return cellContent(cell, 30, false); }));
        save = &engine.enableSave();
    }
    EntityHandle entity(const char* cell, const char* key) { return deltas->find(cell, key); }
};

static void testWorldDeltas() {
    const std::string path = tempPath("delta.sav");
    size_t alive = 0;
    glm::vec3 droppedPos(0.0f);
    {
        DeltaRig rig;
        rig.streaming->prime(0);
        World& w = rig.engine.world();
        CHECK(rig.streaming->resident(0) && rig.streaming->resident(1));
        w.setPosition(rig.entity("C0", "k3"), glm::vec3(9.0f, 8.0f, 7.0f));
        w.setVisible(rig.entity("C0", "k4"), false);
        w.destroy(rig.entity("C0", "k5"));
        w.setTag(rig.entity("C1", "k6"), "hot");
        w.setScale(rig.entity("C1", "k7"), glm::vec3(2.0f, 3.0f, 4.0f));
        EntityDesc d;
        d.name = "dropped";
        d.position = glm::vec3(kSpacing + 1.0f, 2.0f, 3.0f);
        const EntityHandle dropped = w.create(d);
        rig.deltas->trackAdded("C1", dropped);
        rig.engine.portals().cellParams(1).gravity = glm::vec3(0.0f, 0.0f, 9.81f);
        rig.engine.portals().cellParams(1).scalars["wind"] = 3.0f;
        alive = w.aliveCount();
        droppedPos = w.getPosition(dropped);
        std::string error;
        CHECK(rig.save->save(path, "delta", &error));
        CHECK(rig.deltas->deltaCount() >= 7);

        rig.streaming->prime(3);
        CHECK(!rig.streaming->resident(0) && !rig.streaming->resident(1) && rig.streaming->resident(3));
        CHECK(w.find("dropped").isNull());
        rig.streaming->prime(0);
        CHECK(rig.streaming->resident(0) && rig.streaming->resident(1));
        CHECK(glm::length(w.getPosition(rig.entity("C0", "k3")) - glm::vec3(9.0f, 8.0f, 7.0f)) < 1.0e-5f);
        CHECK(!w.isVisible(rig.entity("C0", "k4")));
        CHECK(!w.isAlive(rig.entity("C0", "k5")));
        CHECK(w.getTag(rig.entity("C1", "k6")) == "hot");
        CHECK(!w.find("dropped").isNull() && glm::length(w.getPosition(w.find("dropped")) - droppedPos) < 1.0e-5f);
        CHECK(rig.engine.portals().cellParams(1).gravity.z == 9.81f);
        CHECK(w.aliveCount() == alive);
    }
    {
        DeltaRig rig;
        rig.streaming->prime(0);
        World& w = rig.engine.world();
        std::string error;
        CHECK(rig.save->load(path, &error));
        CHECK(w.aliveCount() == alive);
        CHECK(glm::length(w.getPosition(rig.entity("C0", "k3")) - glm::vec3(9.0f, 8.0f, 7.0f)) < 1.0e-5f);
        CHECK(!w.isVisible(rig.entity("C0", "k4")));
        CHECK(!w.isAlive(rig.entity("C0", "k5")));
        CHECK(w.isVisible(rig.entity("C0", "k6")));
        CHECK(w.getTag(rig.entity("C1", "k6")) == "hot");
        CHECK(glm::length(w.getScale(rig.entity("C1", "k7")) - glm::vec3(2.0f, 3.0f, 4.0f)) < 1.0e-5f);
        const EntityHandle dropped = w.find("dropped");
        CHECK(!dropped.isNull() && glm::length(w.getPosition(dropped) - droppedPos) < 1.0e-5f);
        CHECK(rig.engine.portals().cellParams(1).gravity.z == 9.81f && rig.engine.portals().cellParams(1).scalars["wind"] == 3.0f);
        CHECK(rig.engine.portals().cellParams(0).gravity.y == -9.81f);
        rig.streaming->prime(3);
        rig.streaming->prime(0);
        CHECK(w.aliveCount() == alive);
        CHECK(!w.isAlive(rig.entity("C0", "k5")));
        CHECK(!w.find("dropped").isNull());
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");
}

static void testPortalNavAndAudio() {
    PortalSystem portals;
    buildLine(portals, 2);
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(5.4f, 0.5f, 5.4f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(kSpacing, -0.5f, 0.0f)), glm::vec3(5.4f, 0.5f, 5.4f));
    NavMesh nav;
    nav.setGeometry(soup);
    nav.rebuildAll();
    const glm::vec3 from(-2.0f, 0.0f, 0.0f);
    const glm::vec3 to(kSpacing + 2.0f, 0.0f, 0.0f);
    const NavPath without = nav.findPath(from, to);
    CHECK(!without.found || without.partial);
    PortalNavLinks links;
    CHECK(links.sync(nav, portals) == 1);
    const NavPath with = nav.findPath(from, to);
    CHECK(with.found && !with.partial);
    int linkPoints = 0;
    for (const NavPathPoint& p : with.points) linkPoints += p.link >= 0 ? 1 : 0;
    CHECK(linkPoints >= 1);
    links.clear(nav);
    CHECK(nav.links().empty());

    TriangleSoup walls;
    walls.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(kSpacing * 0.5f, 1.0f, 0.0f)), glm::vec3(0.2f, 5.0f, 6.0f));
    AudioPropagation prop;
    prop.setScene(walls);
    const glm::vec3 listener(0.0f, 1.0f, 0.0f);
    const glm::vec3 source(kSpacing, 1.0f, 0.0f);
    prop.beginFrame(listener);
    const AudioPath blocked = prop.trace(listener, source);
    CHECK(!blocked.viaHop && blocked.directOcclusion == 1.0f);
    prop.setHopHook(portalAudioHook(portals));
    prop.beginFrame(listener);
    const AudioPath hop = prop.trace(listener, source);
    CHECK(hop.viaHop && hop.hopId == 0 && hop.attenuation > blocked.attenuation * 2.0f);
    CHECK(hop.arrivalDirection.x > 0.8f);
}

static void testCellVisibility() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.5f, 0.5f, 0.5f, 0.87f); });
    PortalSystem portals;
    buildLine(portals, 5);
    Camera camera;
    camera.setPos(glm::vec3(-3.2f, 1.7f, 0.0f));
    camera.setOrientation(0.0f, 0.0f);
    camera.updateAspectRatio(1280.0f, 720.0f);
    portals.setCameraCell(0);
    portals.settings().maxDepth = 3;
    const PortalFrame frame = portals.buildFrame(camera, 720.0f);
    const std::vector<VisibleCell> cells = visibleCells(frame, 1);
    CHECK(!cells.empty() && cells[0].cell == 0 && cells[0].depth == 0 && cells[0].shadows);
    bool sawOne = false;
    for (const VisibleCell& v : cells) {
        if (v.cell == 1) sawOne = v.depth == 1 && v.shadows;
        if (v.depth > 1) CHECK(!v.shadows);
        CHECK(v.cell != 4);
    }
    CHECK(sawOne);
}

int main() {
    testStreamingRoute();
    testBudgetedCommitBounds();
    testAssetStreaming();
    testWorldDeltas();
    testPortalNavAndAudio();
    testCellVisibility();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
