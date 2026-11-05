module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

module Engine.Storylet;

namespace {
uint32_t clampedCount(double value) {
    if (!(value > 0.0)) return 0;
    return value >= static_cast<double>(std::numeric_limits<uint32_t>::max()) ? std::numeric_limits<uint32_t>::max() : static_cast<uint32_t>(value);
}

double finiteOrZero(double value) { return std::isfinite(value) ? value : 0.0; }
}

Json StoryletEngine::toJson() const {
    Json result = Json::object();
    result.set("time", m_time);
    Json played = Json::object();
    for (const auto& [id, record] : m_played) {
        Json entry = Json::object();
        entry.set("count", record.count);
        entry.set("last", record.last);
        played.set(id, std::move(entry));
    }
    result.set("played", std::move(played));
    Json resume = Json::object();
    for (const auto& [id, line] : m_resume) resume.set(id, line);
    result.set("resume", std::move(resume));
    Json blocked = Json::object();
    for (const auto& [id, until] : m_blocked) blocked.set(id, until);
    result.set("blocked", std::move(blocked));
    if (m_current) {
        Json current = Json::object();
        current.set("id", m_current->id);
        current.set("line", m_current->line);
        current.set("elapsed", m_current->elapsed);
        result.set("current", std::move(current));
    }
    return result;
}

bool StoryletEngine::fromJson(const Json& doc, std::string* error) {
    if (!doc.isObject()) {
        if (error) *error = "storylet history: not an object";
        return false;
    }
    resetHistory();
    if (const Json* timeJson = doc.get("time")) m_time = finiteOrZero(timeJson->asNumber());
    if (const Json* played = doc.get("played")) {
        for (const auto& [id, entry] : played->members()) {
            Record record;
            if (const Json* count = entry.get("count")) record.count = clampedCount(count->asNumber());
            if (const Json* last = entry.get("last")) record.last = finiteOrZero(last->asNumber());
            m_played[id] = record;
        }
    }
    if (const Json* resume = doc.get("resume")) {
        for (const auto& [id, line] : resume->members()) m_resume[id] = clampedCount(line.asNumber());
    }
    if (const Json* blocked = doc.get("blocked")) {
        for (const auto& [id, until] : blocked->members()) m_blocked[id] = finiteOrZero(until.asNumber());
    }
    const Json* current = doc.get("current");
    if (!current) return true;
    const Json* idJson = current->get("id");
    const Storylet* storylet = idJson ? find(idJson->asString()) : nullptr;
    if (!storylet) {
        if (error) *error = "storylet history: unknown current storylet";
        return false;
    }
    StoryletPlayback playback{storylet->id, 0, 0.0f};
    if (const Json* line = current->get("line")) playback.line = clampedCount(line->asNumber());
    if (const Json* elapsed = current->get("elapsed")) playback.elapsed = std::max(0.0f, static_cast<float>(finiteOrZero(elapsed->asNumber())));
    if (playback.line >= storylet->lines.size()) {
        if (error) *error = "storylet history: bad line index";
        return false;
    }
    m_current = playback;
    return true;
}
