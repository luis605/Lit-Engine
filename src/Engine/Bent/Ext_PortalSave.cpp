#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <unistd.h>

#include "Engine/Bent/Interp.hpp"

import Engine.World;
import Engine.Portal;
import Engine.Delta;
import Engine.Json;
import Engine.Save;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Params;
import Engine.Rng;
import Engine.Events;
import Engine.Render.entity;
import Engine.camera;
import Engine.glm;

#include "Engine/Bent/Ext_PortalUtil.hpp"

namespace bent::xp {

namespace {

struct NamedPosition {
    std::string name;
    glm::dvec3 position;
};

std::vector<NamedPosition> liveEntities(World& world) {
    std::vector<NamedPosition> out;
    world.forEach([&](EntityHandle h) { out.push_back({world.getName(h), world.origin() + glm::dvec3(world.getWorldPosition(h))}); });
    std::sort(out.begin(), out.end(), [](const NamedPosition& a, const NamedPosition& b) { return a.name != b.name ? a.name < b.name : a.position.x < b.position.x; });
    return out;
}

Value livesV(const std::vector<NamedPosition>& lives) {
    ValueList list;
    for (const NamedPosition& e : lives) list.push_back(recV({{"name", strV(e.name)}, {"pos", Value::vec3(e.position.x, e.position.y, e.position.z)}}));
    return listV(std::move(list));
}

struct DeltaRig {
    World world;
    std::unique_ptr<DeltaStore> store;
    std::vector<EntityHandle> bases;
    explicit DeltaRig(const std::vector<glm::vec3>& positions, const glm::vec3& shift = glm::vec3(0.0f)) {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        if (shift != glm::vec3(0.0f)) world.shiftOrigin(glm::dvec3(shift));
        store = std::make_unique<DeltaStore>(world);
        for (size_t i = 0; i < positions.size(); ++i) {
            EntityDesc desc;
            desc.name = "e" + std::to_string(i);
            desc.position = positions[i] - shift;
            bases.push_back(world.create(desc));
            store->registerBase("c", "e" + std::to_string(i), bases.back());
        }
        store->applyCell("c");
    }
};

void registerDelta(Program& p) {
    reg(p, "xdRun", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xdRun");
        const Rec r = rec(a[0], loc);
        std::vector<glm::vec3> positions;
        for (const Value& v : r.list("pos")) positions.push_back(vec3Of(v, loc, "pos"));
        const glm::vec3 shift = r.vec("shift", glm::vec3(0.0f));
        DeltaRig first(positions);
        int added = 0;
        std::vector<EntityHandle> addedRoots;
        for (const Value& item : r.list("ops")) {
            const Rec op = rec(item, loc);
            const std::string name = op.str("op", "");
            const size_t n = first.bases.size();
            if (n == 0 && name != "add") continue;
            EntityHandle target = n ? first.bases[static_cast<size_t>(std::llabs(op.integer("i", 0))) % n] : NULL_ENTITY;
            if (name == "move" && first.world.isAlive(target)) first.world.setPosition(target, first.world.getPosition(target) + op.vec("delta", glm::vec3(0.0f)));
            else if (name == "destroy" && first.world.isAlive(target)) first.world.destroy(target);
            else if (name == "rename" && first.world.isAlive(target)) first.world.setName(target, op.str("name", "renamed"));
            else if (name == "tag" && first.world.isAlive(target)) first.world.setTag(target, op.str("tag", "t"));
            else if (name == "add") {
                EntityDesc desc;
                desc.name = "added" + std::to_string(added++);
                desc.position = op.vec("pos", glm::vec3(0.0f));
                const EntityHandle root = first.world.create(desc);
                first.store->trackAdded("c", root);
                addedRoots.push_back(root);
            }
        }
        const std::vector<NamedPosition> edited = liveEntities(first.world);
        first.store->captureCell("c");
        const size_t count = first.store->deltaCount();
        const std::string text = first.store->toJson().dump();
        const std::string again = [&] {
            first.store->captureCell("c");
            return first.store->toJson().dump();
        }();
        const std::optional<Json> parsed = Json::parse(text);
        DeltaRig second(positions, shift);
        std::string error;
        const bool loaded = parsed && second.store->fromJson(*parsed, &error);
        second.store->applyCell("c");
        const std::vector<NamedPosition> once = liveEntities(second.world);
        second.store->applyCell("c");
        const std::vector<NamedPosition> twice = liveEntities(second.world);
        const std::string reDump = second.store->toJson().dump();
        return recV({{"edited", livesV(edited)}, {"once", livesV(once)}, {"twice", livesV(twice)}, {"loaded", boolV(loaded)}, {"json", strV(text)}, {"deltaCount", intV(static_cast<int64_t>(count))}, {"captureIdempotent", boolV(text == again)}, {"reDumpSame", boolV(reDump == text)}, {"reDump", strV(reDump)}, {"error", strV(error)}});
    });
}

constexpr const char* kSchemaText = R"({
  "vars": {
    "bond": {"type": "range", "min": 0, "max": 1, "default": 0.25},
    "drift": {"type": "range", "min": 0, "max": 1},
    "capacity": {"type": "counter", "min": 1, "max": 6, "default": 3},
    "attends": {"type": "counter", "min": 0, "max": 1000000},
    "met": {"type": "flag"},
    "location_tag": {"type": "set"}
  }
})";

constexpr const char* kStoryletText = R"({
  "storylets": [
    {"id": "coffee", "when": {"all": ["bond >= 0.2", {"location_tag": "home"}, {"not_played": "coffee"}]},
     "priority": 40, "cooldown_seconds": 1200, "delivery": "walking_talk",
     "lines": [{"speaker": "a", "text_id": "coffee_a", "voice_id": "coffee_a", "seconds": 2}, {"speaker": "b", "text_id": "coffee_b", "seconds": 2}],
     "effects": ["bond += 0.05", {"remember": "habit.coffee"}]},
    {"id": "hello", "when": "bond >= 0.2", "priority": 10, "cooldown_seconds": 5,
     "lines": [{"text_id": "hello_a", "seconds": 1}], "effects": ["attends += 1"]},
    {"id": "once_only", "when": {"any": ["met", {"not": "bond > 0.1"}]}, "priority": 5, "once": true,
     "lines": [{"text_id": "o", "seconds": 0.5}], "effects": ["met = false"]}
  ]
})";

struct Session {
    World world;
    StateStore state{&world.events()};
    ParamBus params;
    std::unique_ptr<StoryletEngine> story;
    std::unique_ptr<ObservationSystem> observation;
    std::unique_ptr<SaveSystem> save;
    std::vector<EntityHandle> things;

    explicit Session(uint64_t seed, int entities) {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        world.random().seed(seed);
        state.loadSchema(kSchemaText);
        params.declare("tension", 0.0f, 0.2f, 0.0f, 1.0f);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        story->loadText(kStoryletText);
        observation = std::make_unique<ObservationSystem>(world);
        ObservationConfig config = observation->config();
        config.logLatency = false;
        observation->setConfig(config);
        world.camera().setPos({0.0f, 0.0f, 0.0f});
        for (int i = 0; i < entities; ++i) {
            const float angle = glm::radians(45.0f * static_cast<float>(i));
            things.push_back(world.create("t" + std::to_string(i), 1, {10.0f * std::cos(angle), 0.0f, 10.0f * std::sin(angle)}));
            observation->track(things.back());
        }
        world.events().subscribe<ObservationChanged>([this](const ObservationChanged& e) {
            if (e.to == ObservationState::Attended) {
                state.add("attends", 1);
                state.addMember("location_tag", "home");
            }
        });
        save = std::make_unique<SaveSystem>(SaveContext{&world, &state, story.get(), &params, observation.get()});
        save->addSection("extra", [this]() {
            Json j = Json::object();
            j.set("things", things.size());
            return j;
        }, [](const Json& j, std::string*) { return j.get("things") != nullptr; });
    }

    void step(int tick) {
        const float dt = 1.0f / 60.0f;
        world.camera().setOrientation(-180.0f + static_cast<float>(tick) * 0.9f, 0.0f);
        observation->setView(observationViewFromCamera(world.camera(), 1.0f));
        observation->update(dt);
        auto& rng = world.random().stream("drift");
        state.add("drift", rng.range(0.0f, 0.004f));
        if (tick % 90 == 0) state.add("bond", rng.range(0.0f, 0.2f));
        params.set("tension", static_cast<float>(state.get("drift")));
        params.update(dt);
        story->update(dt);
        world.timeState().frame++;
        world.timeState().elapsed += dt;
    }

    [[nodiscard]] std::string fingerprint() const {
        const Json body = save->capture();
        Json out = Json::object();
        for (const auto& [k, v] : body.members()) {
            if (k != "meta") out.set(k, v);
        }
        return out.dump();
    }
};

std::string uniqueDir() {
    static std::atomic<uint64_t> counter{0};
    return (std::filesystem::temp_directory_path() / ("bent-save-" + std::to_string(::getpid()) + "-" + std::to_string(counter.fetch_add(1)))).string();
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct TempDir {
    std::string path = uniqueDir();
    TempDir() { std::filesystem::create_directories(path); }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    [[nodiscard]] std::string file(const std::string& name) const { return (std::filesystem::path(path) / name).string(); }
};

void registerSave(Program& p) {
    reg(p, "xvContinue", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xvContinue");
        const Rec r = rec(a[0], loc);
        const uint64_t seed = static_cast<uint64_t>(r.integer("seed", 77));
        const int total = static_cast<int>(r.integer("total", 120));
        const int split = static_cast<int>(std::clamp<int64_t>(r.integer("split", 60), 0, total));
        const int entities = static_cast<int>(r.integer("entities", 8));
        const bool viaFile = r.flag("file", true);
        TempDir dir;
        Session full(seed, entities);
        for (int i = 0; i < total; ++i) full.step(i);
        Session first(seed, entities);
        for (int i = 0; i < split; ++i) first.step(i);
        const std::string midPrint = first.fingerprint();
        std::string error;
        Session second(seed + 1000, entities);
        bool loaded = false;
        if (viaFile) {
            const std::string path = dir.file("slot.litsave");
            const bool saved = first.save->save(path, "mid", &error);
            loaded = saved && second.save->load(path, &error);
        } else {
            loaded = second.save->apply(first.save->capture("mid"), &error);
        }
        const std::string loadedPrint = second.fingerprint();
        for (int i = split; i < total; ++i) second.step(i);
        const std::string fullPrint = full.fingerprint();
        const std::string resumedPrint = second.fingerprint();
        for (int i = split; i < total; ++i) first.step(i);
        const bool continuedSame = first.fingerprint() == fullPrint;
        return recV({{"loaded", boolV(loaded)}, {"midSame", boolV(loadedPrint == midPrint)}, {"resumedSame", boolV(resumedPrint == fullPrint)}, {"continuedSame", boolV(continuedSame)}, {"error", strV(error)}, {"attends", numV(full.state.get("attends"))}, {"bond", numV(full.state.get("bond"))}, {"played", intV(static_cast<int64_t>(full.story->playCount("hello")))}});
    });

    reg(p, "xvCorrupt", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xvCorrupt");
        const Rec r = rec(a[0], loc);
        const std::string mode = r.str("mode", "flip");
        const uint64_t seed = static_cast<uint64_t>(r.integer("seed", 5));
        TempDir dir;
        Session writer(seed, 4);
        for (int i = 0; i < 40; ++i) writer.step(i);
        const std::string path = dir.file("slot.litsave");
        std::string error;
        const bool saved = writer.save->save(path, "label", &error);
        const std::string text = readFile(path);
        std::string damaged = text;
        const size_t position = text.empty() ? 0 : static_cast<size_t>(std::llabs(r.integer("at", 0))) % text.size();
        if (mode == "flip") {
            damaged[position] = static_cast<char>(damaged[position] ^ static_cast<char>(1 + (r.integer("bit", 0) & 0x3f)));
        } else if (mode == "truncate") {
            damaged.resize(position);
        } else if (mode == "version") {
            const size_t at = damaged.find("\"version\":1");
            if (at != std::string::npos) damaged.replace(at, 11, "\"version\":" + r.str("version", "0"));
        } else if (mode == "checksum") {
            const size_t at = damaged.find("\"checksum\":\"");
            if (at != std::string::npos) damaged[at + 12] = damaged[at + 12] == '0' ? '1' : '0';
        } else if (mode == "format") {
            const size_t at = damaged.find("litsave");
            if (at != std::string::npos) damaged[at] = 'x';
        } else if (mode == "empty") {
            damaged.clear();
        }
        writeFile(dir.file("damaged.litsave"), damaged);
        Session reader(seed + 1, 4);
        for (int i = 0; i < 17; ++i) reader.step(i);
        const std::string before = reader.fingerprint();
        const bool ok = reader.save->load(dir.file("damaged.litsave"), &error);
        const std::string after = reader.fingerprint();
        const bool same = damaged == text;
        const bool matchesWriter = after == [&] {
            Session reference(seed + 1, 4);
            for (int i = 0; i < 17; ++i) reference.step(i);
            std::string ignored;
            reference.save->load(path, &ignored);
            return reference.fingerprint();
        }();
        return recV({{"saved", boolV(saved)}, {"ok", boolV(ok)}, {"unchanged", boolV(before == after)}, {"unmodifiedFile", boolV(same)}, {"acceptedIsOriginal", boolV(matchesWriter)}, {"error", strV(error)}, {"bytes", intV(static_cast<int64_t>(text.size()))}});
    });

    reg(p, "xvBadApply", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xvBadApply");
        const Rec r = rec(a[0], loc);
        const std::string mode = r.str("mode", "seed");
        Session source(9, 4);
        for (int i = 0; i < 30; ++i) source.step(i);
        Json body = source.save->capture("x");
        if (mode == "seed") {
            Json random = *body.get("random");
            random.set("seed", "not-hex");
            body.set("random", std::move(random));
        } else if (mode == "state") {
            Json copy = Json::object();
            for (const auto& [k, v] : body.members()) {
                if (k != "state") copy.set(k, v);
            }
            body = std::move(copy);
        } else if (mode == "sections") {
            Json copy = Json::object();
            for (const auto& [k, v] : body.members()) copy.set(k, k == "sections" ? Json::object() : v);
            body = std::move(copy);
        } else if (mode == "stream") {
            Json random = *body.get("random");
            Json streams = Json::object();
            Json bad = Json::object();
            bad.set("state", "zz");
            bad.set("increment", "01");
            streams.set("drift", std::move(bad));
            random.set("streams", std::move(streams));
            body.set("random", std::move(random));
        } else if (mode == "storylets") {
            Json copy = Json::object();
            for (const auto& [k, v] : body.members()) copy.set(k, k == "storylets" ? Json("garbage") : v);
            body = std::move(copy);
        } else if (mode == "notObject") {
            body = Json("nope");
        }
        Session target(1234, 4);
        for (int i = 0; i < 11; ++i) target.step(i);
        const std::string before = target.fingerprint();
        std::string error;
        const bool ok = target.save->apply(body, &error);
        const std::string after = target.fingerprint();
        return recV({{"ok", boolV(ok)}, {"unchanged", boolV(before == after)}, {"error", strV(error)}});
    });

    reg(p, "xvFiles", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xvFiles");
        const Rec r = rec(a[0], loc);
        TempDir dir;
        Session session(static_cast<uint64_t>(r.integer("seed", 3)), 3);
        const std::string path = dir.file("slot.litsave");
        std::string error;
        session.save->save(path, "one", &error);
        const std::string firstText = readFile(path);
        for (int i = 0; i < 25; ++i) session.step(i);
        session.save->save(path, "two", &error);
        const std::string secondText = readFile(path);
        const std::string backup = readFile(path + ".bak");
        int tmp = 0;
        int files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
            ++files;
            tmp += entry.path().extension() == ".tmp" ? 1 : 0;
        }
        Session other(static_cast<uint64_t>(r.integer("seed", 3)), 3);
        for (int i = 0; i < 25; ++i) other.step(i);
        const std::string asyncPath = dir.file("async.litsave");
        other.save->saveAsync(asyncPath, "two");
        const bool waited = other.save->waitPending();
        const std::string asyncText = readFile(asyncPath);
        return recV({{"tmp", intV(tmp)}, {"files", intV(files)}, {"backupIsFirst", boolV(backup == firstText)}, {"differs", boolV(firstText != secondText)}, {"asyncOk", boolV(waited)}, {"asyncSameBody", boolV(!asyncText.empty())}, {"serial", intV(static_cast<int64_t>(session.save->serial()))}});
    });
}

}

void registerExtPortalSave(Program& program) {
    registerDelta(program);
    registerSave(program);
}

}
