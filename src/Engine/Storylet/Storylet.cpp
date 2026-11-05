module;

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

module Engine.Storylet;

namespace {
bool compare(StoryletCondition::Cmp comparison, double left, double right) {
    switch (comparison) {
        case StoryletCondition::Cmp::Ge: return left >= right;
        case StoryletCondition::Cmp::Le: return left <= right;
        case StoryletCondition::Cmp::Gt: return left > right;
        case StoryletCondition::Cmp::Lt: return left < right;
        case StoryletCondition::Cmp::Eq: return left == right;
        case StoryletCondition::Cmp::Ne: return left != right;
    }
    return false;
}
}

StoryletEngine::StoryletEngine(StateStore& state, RandomService& random, EventBus* bus, std::string memorySet)
    : m_state(state), m_random(random), m_bus(bus), m_memorySet(std::move(memorySet)) {
    if (!m_state.has(m_memorySet)) m_state.declare(m_memorySet, VarKind::Set);
}

void StoryletEngine::clear() {
    m_storylets.clear();
    resetHistory();
}

void StoryletEngine::resetHistory() {
    m_played.clear();
    m_resume.clear();
    m_blocked.clear();
    m_current.reset();
    m_time = 0.0;
}

const Storylet* StoryletEngine::find(const std::string& id) const {
    const auto found = std::lower_bound(m_storylets.begin(), m_storylets.end(), id, [](const Storylet& storylet, const std::string& key) { return storylet.id < key; });
    return found != m_storylets.end() && found->id == id ? &*found : nullptr;
}

uint32_t StoryletEngine::playCount(const std::string& id) const {
    const auto found = m_played.find(id);
    return found == m_played.end() ? 0 : found->second.count;
}

bool StoryletEngine::evaluate(const StoryletCondition& condition) const {
    using Op = StoryletCondition::Op;
    const auto childHolds = [this](const StoryletCondition& child) { return evaluate(child); };
    switch (condition.op) {
        case Op::All: return std::all_of(condition.children.begin(), condition.children.end(), childHolds);
        case Op::Any: return std::any_of(condition.children.begin(), condition.children.end(), childHolds);
        case Op::Not: return condition.children.empty() || !evaluate(condition.children.front());
        case Op::Compare: return compare(condition.cmp, m_state.get(condition.name), condition.number);
        case Op::Flag: return m_state.get(condition.name) != 0.0;
        case Op::Member: return m_state.hasMember(condition.name, condition.member);
        case Op::Played: return m_played.contains(condition.name);
    }
    return false;
}

bool StoryletEngine::available(const Storylet& storylet) const {
    const auto played = m_played.find(storylet.id);
    if (played != m_played.end()) {
        if (storylet.once) return false;
        if (storylet.cooldownSeconds > 0.0 && m_time - played->second.last < storylet.cooldownSeconds) return false;
    }
    if (const auto blocked = m_blocked.find(storylet.id); blocked != m_blocked.end() && m_time < blocked->second) return false;
    return evaluate(storylet.when);
}

bool StoryletEngine::eligible(const std::string& id) const {
    const Storylet* storylet = find(id);
    return storylet && available(*storylet);
}

std::vector<std::string> StoryletEngine::eligibleIds() const {
    std::vector<std::string> ids;
    for (const Storylet& storylet : m_storylets) {
        if (available(storylet)) ids.push_back(storylet.id);
    }
    return ids;
}

void StoryletEngine::beginLine(const Storylet& storylet) {
    m_current->elapsed = 0.0f;
    if (!m_bus) return;
    const StoryletLine line = storylet.lines[m_current->line];
    m_bus->emitNow(StoryletLineStarted{storylet.id, m_current->line, line.speaker, line.textId, line.voiceId});
}

void StoryletEngine::start(const Storylet& storylet) {
    const auto resume = m_resume.find(storylet.id);
    const bool resumed = resume != m_resume.end();
    uint32_t line = resumed ? resume->second : 0;
    if (line >= storylet.lines.size()) line = 0;
    m_current = StoryletPlayback{storylet.id, line, 0.0f};
    if (resumed) m_resume.erase(storylet.id);
    if (m_bus) m_bus->emitNow(StoryletStarted{storylet.id, resumed});
    if (!m_current) return;
    if (storylet.lines.empty()) complete(storylet);
    else beginLine(storylet);
}

void StoryletEngine::applyEffects(const Storylet& storylet) {
    for (const StoryletEffect& effect : storylet.effects) {
        switch (effect.op) {
            case StoryletEffect::Op::Set: m_state.set(effect.name, effect.number); break;
            case StoryletEffect::Op::Add: m_state.add(effect.name, effect.number); break;
            case StoryletEffect::Op::Remember: m_state.addMember(m_memorySet, effect.member); break;
            case StoryletEffect::Op::AddMember: m_state.addMember(effect.name, effect.member); break;
            case StoryletEffect::Op::RemoveMember: m_state.removeMember(effect.name, effect.member); break;
        }
    }
}

void StoryletEngine::complete(const Storylet& storylet) {
    m_current.reset();
    m_resume.erase(storylet.id);
    Record& record = m_played[storylet.id];
    ++record.count;
    record.last = m_time;
    applyEffects(storylet);
    if (m_bus) m_bus->emitNow(StoryletCompleted{storylet.id});
}

bool StoryletEngine::interrupt(const std::string& reason) {
    if (!m_current) return false;
    const Storylet* storylet = find(m_current->id);
    if (!storylet || !storylet->interruptible) return false;
    const StoryletPlayback playback = *m_current;
    m_current.reset();
    m_resume[playback.id] = playback.line;
    m_blocked[playback.id] = m_time + m_retryDelay;
    if (m_bus) {
        m_bus->emitNow(StoryletLineEnded{playback.id, playback.line, true});
        m_bus->emitNow(StoryletInterrupted{playback.id, reason, playback.line});
    }
    return true;
}

void StoryletEngine::select() {
    const Storylet* best = nullptr;
    m_tieScratch.clear();
    for (const Storylet& storylet : m_storylets) {
        if (!available(storylet)) continue;
        if (!best || storylet.priority > best->priority) {
            best = &storylet;
            m_tieScratch.assign(1, &storylet);
        } else if (storylet.priority == best->priority) {
            m_tieScratch.push_back(&storylet);
        }
    }
    if (!best) return;
    if (m_current) {
        const Storylet* playing = find(m_current->id);
        if (!playing || best->priority <= playing->priority || !playing->interruptible) return;
        interrupt("outranked");
    }
    const Storylet* pick = m_tieScratch.size() == 1 ? m_tieScratch.front() : m_tieScratch[m_random.stream("storylet").below(static_cast<uint32_t>(m_tieScratch.size()))];
    start(*pick);
}

void StoryletEngine::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    m_time += dt;
    select();
    float remaining = dt;
    while (m_current && remaining >= 0.0f) {
        const Storylet* storylet = find(m_current->id);
        if (!storylet || m_current->line >= storylet->lines.size()) {
            m_current.reset();
            break;
        }
        const float needed = storylet->lines[m_current->line].seconds - m_current->elapsed;
        if (remaining < needed) {
            m_current->elapsed += remaining;
            break;
        }
        remaining -= needed;
        if (m_bus) m_bus->emitNow(StoryletLineEnded{storylet->id, m_current->line, false});
        if (!m_current) break;
        if (m_current->line + 1 >= storylet->lines.size()) {
            complete(*storylet);
            break;
        }
        ++m_current->line;
        beginLine(*storylet);
    }
}
