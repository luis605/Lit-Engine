#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Engine/Bent/Interp.hpp"

import Engine.World;
import Engine.Portal;
import Engine.Streaming;
import Engine.Render.entity;
import Engine.glm;

#include "Engine/Bent/Ext_PortalUtil.hpp"

namespace bent::xp {

namespace {

ResidentKind kindOfInt(int64_t value) { return static_cast<ResidentKind>(std::clamp<int64_t>(value, 0, static_cast<int64_t>(kResidentKinds) - 1)); }

ResidencyConfig configOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    ResidencyConfig config;
    config.hopMetres = r.f("hopMetres", config.hopMetres);
    config.minSpeed = r.f("minSpeed", config.minSpeed);
    config.outOfViewSeconds = r.f("outOfViewSeconds", config.outOfViewSeconds);
    config.residentBias = r.f("residentBias", config.residentBias);
    config.byteBudget = static_cast<size_t>(std::max<int64_t>(r.integer("byteBudget", 0), 0));
    config.deterministic = r.flag("deterministic", false);
    const ValueList& bias = r.list("kindBias");
    for (size_t i = 0; i < bias.size() && i < kResidentKinds; ++i) config.kindBias[i] = static_cast<float>(argNumber(bias[i], loc, "kindBias"));
    return config;
}

ResidencyDemand demandOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    ResidencyDemand demand;
    demand.kind = kindOfInt(r.integer("kind", 0));
    demand.space = static_cast<int>(r.integer("space", 0));
    demand.direction = r.vec("direction", glm::vec3(0.0f));
    demand.distance = r.f("distance", 0.0f);
    demand.radius = r.f("radius", 0.0f);
    demand.hops = static_cast<int>(r.integer("hops", 0));
    demand.inView = r.flag("inView", true);
    demand.resident = r.flag("resident", false);
    return demand;
}

void registerResidency(Program& p) {
    reg(p, "xrScore", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xrScore");
        ResidencyManager manager(1, configOf(a[0], loc));
        manager.setVelocity(0, vec3Of(a[2], loc, "velocity"));
        manager.setVelocity(1, vec3Of(a[2], loc, "velocity"));
        return numV(manager.score(demandOf(a[1], loc)));
    });
    reg(p, "xrPoolPriority", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xrPoolPriority");
        return intV(ResidencyManager::poolPriority(static_cast<float>(argNumber(a[0], loc, "score"))));
    });

    reg(p, "xrEvict", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xrEvict");
        ResidencyManager manager(1, configOf(a[0], loc));
        std::vector<int64_t> evicted;
        std::vector<int64_t> calls;
        const ValueList& items = argList(a[1], loc, "items");
        for (size_t index = 0; index < items.size(); ++index) {
            const Rec item = rec(items[index], loc);
            const int64_t id = item.integer("id", static_cast<int64_t>(index));
            const ResidentKind kind = kindOfInt(item.integer("kind", 0));
            const bool evictable = item.flag("evictable", true);
            const bool immediate = item.flag("immediate", true);
            const bool refuses = item.flag("refuses", false);
            ResidencyManager* self = &manager;
            std::function<bool()> evict;
            if (evictable) {
                evict = [&, id, kind, immediate, refuses, self]() -> bool {
                    calls.push_back(id);
                    if (refuses) return false;
                    evicted.push_back(id);
                    if (immediate) self->untrack(kind, static_cast<uint64_t>(id));
                    return true;
                };
            }
            manager.track(kind, static_cast<uint64_t>(id), static_cast<size_t>(item.integer("bytes", 0)), item.f("score", 0.0f), item.flag("protected", false), std::move(evict));
        }
        const size_t before = manager.residentBytes();
        const size_t requested = manager.enforceByteBudget();
        const size_t after = manager.residentBytes();
        const size_t firstCalls = calls.size();
        const size_t again = manager.enforceByteBudget();
        ValueList evictedList;
        for (int64_t id : evicted) evictedList.push_back(intV(id));
        ValueList callList;
        for (size_t i = 0; i < firstCalls; ++i) callList.push_back(intV(calls[i]));
        return recV({{"evicted", listV(std::move(evictedList))}, {"calls", listV(std::move(callList))}, {"before", intV(static_cast<int64_t>(before))}, {"after", intV(static_cast<int64_t>(after))}, {"requested", intV(static_cast<int64_t>(requested))}, {"again", intV(static_cast<int64_t>(again))}, {"memoryEvictions", intV(static_cast<int64_t>(manager.stats().memoryEvictions))}});
    });

    reg(p, "xrFlush", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xrFlush");
        ResidencyManager manager(1, ResidencyConfig{});
        const Rec b = rec(a[0], loc);
        StreamBudget budget;
        budget.bytes = static_cast<size_t>(std::max<int64_t>(b.integer("bytes", 1024), 0));
        budget.millis = 1.0e9;
        budget.unlimited = b.flag("unlimited", false);
        std::vector<int64_t> order;
        std::vector<int64_t> spentBefore;
        const ValueList& tasks = argList(a[1], loc, "tasks");
        int owner = 0;
        for (size_t index = 0; index < tasks.size(); ++index) {
            const Rec t = rec(tasks[index], loc);
            const size_t cost = static_cast<size_t>(std::max<int64_t>(t.integer("cost", 0), 0));
            const int64_t id = static_cast<int64_t>(index);
            manager.offer(&owner, kindOfInt(t.integer("kind", 0)), t.f("score", 0.0f), [&order, &spentBefore, id, cost](StreamBudget& run) {
                order.push_back(id);
                spentBefore.push_back(static_cast<int64_t>(run.spent));
                run.spend(cost);
            });
        }
        const size_t ran = manager.flush(budget);
        ValueList orderList;
        for (int64_t id : order) orderList.push_back(intV(id));
        ValueList spentList;
        for (int64_t s : spentBefore) spentList.push_back(intV(s));
        return recV({{"ran", intV(static_cast<int64_t>(ran))}, {"order", listV(std::move(orderList))}, {"spentBefore", listV(std::move(spentList))}, {"spent", intV(static_cast<int64_t>(budget.spent))}, {"deferred", intV(static_cast<int64_t>(manager.stats().commitsDeferred))}});
    });

    reg(p, "xrJobs", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xrJobs");
        const ValueList& jobs = argList(a[0], loc, "jobs");
        std::mutex mutex;
        std::vector<std::pair<int64_t, bool>> executed;
        std::atomic<bool> release{false};
        std::atomic<bool> started{false};
        std::vector<std::shared_ptr<std::atomic<bool>>> tokens;
        {
            AsyncJobs pool(1);
            pool.submit(-1000000, [&](bool) {
                started = true;
                while (!release.load()) std::this_thread::sleep_for(std::chrono::microseconds(50));
            }, nullptr);
            while (!started.load()) std::this_thread::sleep_for(std::chrono::microseconds(50));
            for (size_t index = 0; index < jobs.size(); ++index) {
                const Rec j = rec(jobs[index], loc);
                auto token = std::make_shared<std::atomic<bool>>(false);
                tokens.push_back(token);
                const int64_t id = static_cast<int64_t>(index);
                pool.submit(static_cast<int>(j.integer("priority", 0)), [&, id](bool cancelled) {
                    std::lock_guard lock(mutex);
                    executed.emplace_back(id, cancelled);
                }, token);
            }
            for (size_t index = 0; index < jobs.size(); ++index) {
                if (rec(jobs[index], loc).flag("cancel", false)) tokens[index]->store(true);
            }
            release = true;
            pool.waitIdle();
        }
        ValueList list;
        for (const auto& [id, cancelled] : executed) list.push_back(recV({{"id", intV(id)}, {"cancelled", boolV(cancelled)}}));
        return listV(std::move(list));
    });
}

struct StreamEvents {
    std::mutex mutex;
    std::vector<std::pair<std::string, int>> events;
    std::map<int, int> prepares;
    uint64_t bytesThisUpdate = 0;
};

void registerStreaming(Program& p) {
    reg(p, "xsRun", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xsRun");
        const Rec r = rec(a[0], loc);
        const int nx = static_cast<int>(r.integer("nx", 4));
        const int nz = static_cast<int>(r.integer("nz", 4));
        const float cell = r.f("cell", 50.0f);
        PortalSystem portals;
        for (int x = 0; x < nx; ++x) {
            for (int z = 0; z < nz; ++z) {
                PortalCell c;
                c.name = "G" + std::to_string(x) + "_" + std::to_string(z);
                c.min = glm::vec3(static_cast<float>(x) * cell, -100.0f, static_cast<float>(z) * cell);
                c.max = glm::vec3(static_cast<float>(x + 1) * cell, 1000.0f, static_cast<float>(z + 1) * cell);
                portals.addCell(c);
            }
        }
        StreamingConfig config;
        config.spatial.enabled = true;
        config.spatial.loadRadius = r.f("load", 60.0f);
        config.spatial.keepRadius = r.f("keep", 90.0f);
        config.spatial.lodHysteresis = r.f("lodHysteresis", 10.0f);
        config.spatial.moveThreshold = r.f("moveThreshold", 1.0f);
        config.spatial.reloadOnLodChange = r.flag("reloadOnLodChange", true);
        for (const Value& radius : r.list("lodRadii")) config.spatial.lodRadii.push_back(static_cast<float>(argNumber(radius, loc, "lodRadius")));
        config.loadHops = static_cast<int>(r.integer("loadHops", 0));
        config.keepHops = static_cast<int>(r.integer("keepHops", 0));
        config.workers = static_cast<uint32_t>(r.integer("workers", 2));
        config.maxInFlight = static_cast<uint32_t>(r.integer("maxInFlight", 64));
        config.deterministic = r.flag("deterministic", true);
        config.uploadBytesPerFrame = static_cast<size_t>(r.integer("uploadBytes", 4 << 20));
        config.commitMillisPerFrame = r.num("commitMillis", 1000.0);
        config.residency.byteBudget = static_cast<size_t>(std::max<int64_t>(r.integer("byteBudget", 0), 0));
        const int64_t cellBytes = r.integer("cellBytes", 1000);
        const int64_t prepareMicros = r.integer("prepareMicros", 0);
        const bool useLod = r.flag("useLod", false);
        const int64_t commitSteps = std::max<int64_t>(r.integer("commitSteps", 1), 1);
        auto events = std::make_shared<StreamEvents>();
        StreamingSystem streaming(portals, config);
        CellLoader loader;
        const auto prepare = [events, prepareMicros](int cellIndex) {
            if (prepareMicros > 0) std::this_thread::sleep_for(std::chrono::microseconds(prepareMicros));
            std::lock_guard lock(events->mutex);
            ++events->prepares[cellIndex];
            return std::shared_ptr<void>(std::make_shared<int>(cellIndex));
        };
        if (useLod) {
            loader.prepareLod = [prepare](int cellIndex, int) { return prepare(cellIndex); };
        } else {
            loader.prepare = prepare;
        }
        auto stepsLeft = std::make_shared<std::map<int, int64_t>>();
        loader.commit = [events, cellBytes, commitSteps, stepsLeft](int cellIndex, void*, StreamBudget& budget) {
            int64_t& left = (*stepsLeft)[cellIndex];
            if (left == 0) left = commitSteps;
            budget.spend(static_cast<size_t>(cellBytes / commitSteps));
            events->bytesThisUpdate += static_cast<uint64_t>(cellBytes / commitSteps);
            if (--left > 0) return false;
            std::lock_guard lock(events->mutex);
            events->events.emplace_back("commit", cellIndex);
            return true;
        };
        loader.unload = [events](int cellIndex, StreamBudget&) {
            std::lock_guard lock(events->mutex);
            events->events.emplace_back("unload", cellIndex);
            return true;
        };
        loader.evicting = [events](int cellIndex) {
            std::lock_guard lock(events->mutex);
            events->events.emplace_back("evicting", cellIndex);
        };
        streaming.setLoader(loader);

        const auto cameraCellOf = [&](const glm::vec3& position) {
            const int found = portals.cellAt(position);
            return found >= 0 ? found : 0;
        };
        ValueList residentPerStep;
        ValueList bytesPerStep;
        ValueList eventMarks;
        bool primeResult = false;
        const bool prime = r.flag("prime", false);
        const ValueList& path = r.list("path");
        const int settleUpdates = static_cast<int>(r.integer("settle", 1));
        const bool sleepBetween = !config.deterministic;
        for (size_t step = 0; step < path.size(); ++step) {
            const glm::vec3 position = vec3Of(path[step], loc, "path");
            uint64_t worstUpdate = 0;
            if (prime && step == 0) {
                events->bytesThisUpdate = 0;
                primeResult = streaming.prime(cameraCellOf(position), position);
                worstUpdate = events->bytesThisUpdate;
            } else {
                for (int u = 0; u < settleUpdates; ++u) {
                    events->bytesThisUpdate = 0;
                    streaming.update(cameraCellOf(position), position);
                    worstUpdate = std::max(worstUpdate, events->bytesThisUpdate);
                    streaming.recordFrame(1.0);
                    if (sleepBetween) std::this_thread::sleep_for(std::chrono::microseconds(300));
                }
            }
            bytesPerStep.push_back(intV(static_cast<int64_t>(worstUpdate)));
            ValueList resident;
            for (int c : streaming.residentCells()) resident.push_back(intV(c));
            residentPerStep.push_back(listV(std::move(resident)));
            std::lock_guard lock(events->mutex);
            eventMarks.push_back(intV(static_cast<int64_t>(events->events.size())));
        }
        if (!path.empty()) {
            const glm::vec3 last = vec3Of(path.back(), loc, "path");
            for (int i = 0; i < 4000 && !streaming.settled(); ++i) {
                streaming.update(cameraCellOf(last), last);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            streaming.update(cameraCellOf(last), last);
        }
        ValueList eventList;
        {
            std::lock_guard lock(events->mutex);
            for (const auto& [kind, cellIndex] : events->events) eventList.push_back(recV({{"kind", strV(kind)}, {"cell", intV(cellIndex)}}));
        }
        ValueList lods;
        ValueList distances;
        ValueList wanted;
        ValueList states;
        ValueList prepareCounts;
        const int total = nx * nz;
        for (int c = 0; c < total; ++c) {
            lods.push_back(intV(streaming.lod(c)));
            distances.push_back(numV(streaming.spatialDistance(c)));
            wanted.push_back(boolV(streaming.wanted(c)));
            states.push_back(intV(static_cast<int>(streaming.state(c))));
            std::lock_guard lock(events->mutex);
            prepareCounts.push_back(intV(events->prepares[c]));
        }
        ValueList finalResident;
        for (int c : streaming.residentCells()) finalResident.push_back(intV(c));
        const StreamingStats stats = streaming.stats();
        return recV({{"residentPerStep", listV(std::move(residentPerStep))},
                     {"bytesPerStep", listV(std::move(bytesPerStep))},
                     {"eventMarks", listV(std::move(eventMarks))},
                     {"events", listV(std::move(eventList))},
                     {"finalResident", listV(std::move(finalResident))},
                     {"lods", listV(std::move(lods))},
                     {"distances", listV(std::move(distances))},
                     {"wanted", listV(std::move(wanted))},
                     {"states", listV(std::move(states))},
                     {"prepares", listV(std::move(prepareCounts))},
                     {"settled", boolV(streaming.settled())},
                     {"prime", boolV(primeResult)},
                     {"loaded", intV(static_cast<int64_t>(stats.cellsLoaded))},
                     {"unloaded", intV(static_cast<int64_t>(stats.cellsUnloaded))},
                     {"cancelled", intV(static_cast<int64_t>(stats.cellsCancelled))},
                     {"jobsCancelled", intV(static_cast<int64_t>(stats.jobsCancelled))},
                     {"lodReloads", intV(static_cast<int64_t>(stats.lodReloads))},
                     {"residentBytes", intV(static_cast<int64_t>(stats.residentBytes))},
                     {"memoryEvictions", intV(static_cast<int64_t>(stats.memoryEvictions))},
                     {"pendingCells", intV(stats.pendingCells)}});
    });

    reg(p, "xsGraph", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xsGraph");
        const int cells = static_cast<int>(argInt(a[0], loc, "cells"));
        PortalSystem portals;
        for (int i = 0; i < cells; ++i) {
            PortalCell c;
            c.name = "c" + std::to_string(i);
            portals.addCell(c);
        }
        for (const Value& item : argList(a[1], loc, "pairs")) {
            const Rec pair = rec(item, loc);
            PortalEnd endA;
            PortalEnd endB;
            endA.cell = "c" + std::to_string(pair.integer("cellA", 0));
            endB.cell = "c" + std::to_string(pair.integer("cellB", 1));
            endB.position = glm::vec3(10.0f, 0.0f, 0.0f);
            portals.addPair("g", endA, endB);
        }
        StreamingSystem streaming(portals, StreamingConfig{});
        ValueList out;
        for (int d : streaming.graphDistances(static_cast<int>(argInt(a[2], loc, "from")))) out.push_back(intV(d == std::numeric_limits<int>::max() ? -1 : d));
        return listV(std::move(out));
    });
}

}

void registerExtPortalStreaming(Program& program) {
    registerResidency(program);
    registerStreaming(program);
}

}
