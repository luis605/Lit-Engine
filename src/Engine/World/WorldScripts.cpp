module;

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.glm;

namespace {

constexpr int kMaxTimerFiresPerUpdate = 1000;

}

void World::runScriptDestroy(Entity index) {
    const auto scriptsIt = m_scripts.find(index);
    if (scriptsIt == m_scripts.end()) return;
    const EntityHandle handle{index, m_generation[index]};
    auto scripts = std::move(scriptsIt->second);
    m_scripts.erase(scriptsIt);
    for (auto& script : scripts) {
        if (script->m_started) script->onDestroy(*this, handle);
    }
}

Script* World::addScript(EntityHandle entity, std::unique_ptr<Script> script) {
    if (!valid(entity) || !script) return nullptr;
    Script* raw = script.get();
    m_scripts[entity.index].push_back(std::move(script));
    return raw;
}

void World::removeScripts(EntityHandle entity) {
    if (!valid(entity)) return;
    if (m_updating) {
        m_pendingRemoveScripts.push_back(entity);
        return;
    }
    runScriptDestroy(entity.index);
}

void World::flushPendingDestroy() {
    auto pending = std::move(m_pendingDestroy);
    m_pendingDestroy.clear();
    for (EntityHandle handle : pending) destroy(handle);
}

void World::runScripts(const std::function<void(Script&, EntityHandle)>& fn) {
    std::vector<std::pair<EntityHandle, Script*>> snapshot;
    for (auto& [index, scripts] : m_scripts) {
        for (auto& script : scripts) snapshot.emplace_back(EntityHandle{index, m_generation[index]}, script.get());
    }
    std::stable_sort(snapshot.begin(), snapshot.end(), [](const auto& left, const auto& right) { return left.first.index < right.first.index; });

    m_updating = true;
    for (auto& [handle, script] : snapshot) {
        if (!script->m_started) {
            script->m_started = true;
            script->onStart(*this, handle);
        }
        fn(*script, handle);
    }
    m_updating = false;
    auto removals = std::move(m_pendingRemoveScripts);
    m_pendingRemoveScripts.clear();
    for (EntityHandle handle : removals) removeScripts(handle);
    flushPendingDestroy();
}

void World::update(float deltaTime) {
    runScripts([&](Script& script, EntityHandle handle) { script.onUpdate(*this, handle, deltaTime); });
    runTimers(deltaTime);
}

void World::fixedUpdate(float fixedDelta) {
    runScripts([&](Script& script, EntityHandle handle) { script.onFixedUpdate(*this, handle, fixedDelta); });
}

uint32_t World::after(float seconds, TimerFn fn, EntityHandle owner) {
    if (!fn) return 0;
    const uint32_t id = ++m_nextTimerId;
    m_timers.push_back({id, std::max(0.0, static_cast<double>(seconds)), 0.0, std::move(fn), owner, false});
    return id;
}

uint32_t World::every(float seconds, TimerFn fn, EntityHandle owner) {
    if (!fn || !(seconds > 0.0f)) return 0;
    const uint32_t id = ++m_nextTimerId;
    m_timers.push_back({id, static_cast<double>(seconds), static_cast<double>(seconds), std::move(fn), owner, false});
    return id;
}

bool World::cancel(uint32_t timerId) {
    for (Timer& timer : m_timers) {
        if (timer.id != timerId || timer.cancelled) continue;
        timer.cancelled = true;
        if (!m_firingTimers) std::erase_if(m_timers, [](const Timer& candidate) { return candidate.cancelled; });
        return true;
    }
    return false;
}

void World::runTimers(float deltaTime) {
    if (m_timers.empty()) return;
    m_firingTimers = true;
    const size_t count = m_timers.size();
    for (size_t slot = 0; slot < count && slot < m_timers.size(); ++slot) {
        if (m_timers[slot].cancelled) continue;
        if (!m_timers[slot].owner.isNull() && !valid(m_timers[slot].owner)) {
            m_timers[slot].cancelled = true;
            continue;
        }
        m_timers[slot].remaining -= deltaTime;
        int fired = 0;
        while (m_timers[slot].remaining <= 0.0 && !m_timers[slot].cancelled && fired < kMaxTimerFiresPerUpdate) {
            ++fired;
            const TimerFn callback = m_timers[slot].fn;
            const EntityHandle owner = m_timers[slot].owner;
            const bool repeating = m_timers[slot].interval > 0.0;
            if (repeating) {
                m_timers[slot].remaining += m_timers[slot].interval;
            } else {
                m_timers[slot].cancelled = true;
            }
            callback(*this, owner);
            if (!repeating) break;
        }
    }
    m_firingTimers = false;
    std::erase_if(m_timers, [](const Timer& timer) { return timer.cancelled; });
}
