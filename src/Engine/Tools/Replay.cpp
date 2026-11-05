module;

#include <bitset>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Engine.Replay;
import Engine.Json;

namespace {
constexpr size_t kFloatBufferSize = 32;
constexpr size_t kMaxReplayTicks = 100000000;
constexpr size_t kSeedPrefixLength = 5;
constexpr size_t kTicksPrefixLength = 6;
constexpr size_t kHashIntervalPrefixLength = 13;
constexpr size_t kHashPrefixLength = 5;
constexpr int kHexBase = 16;

struct ReplayEvent {
    size_t tick = 0;
    std::string fields;
};

void appendFloat(std::string& out, float value) {
    char buffer[kFloatBufferSize];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

void appendVec2(std::string& out, const glm::vec2& value) {
    appendFloat(out, value.x);
    out += ',';
    appendFloat(out, value.y);
}

bool parseFloat(std::string_view text, float& out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseVec2(std::string_view text, glm::vec2& out) {
    const size_t comma = text.find(',');
    if (comma == std::string_view::npos) return false;
    return parseFloat(text.substr(0, comma), out.x) && parseFloat(text.substr(comma + 1), out.y);
}

template <typename Integer>
bool parseInt(std::string_view text, Integer& out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parseKeyList(std::string_view value, std::bitset<kInputKeyCount>& keys) {
    keys.reset();
    if (value == "-") return true;
    size_t position = 0;
    while (position <= value.size()) {
        size_t comma = value.find(',', position);
        if (comma == std::string_view::npos) comma = value.size();
        int key = 0;
        if (!parseInt(value.substr(position, comma - position), key) || key < 0 || key >= kInputKeyCount) return false;
        keys.set(static_cast<size_t>(key));
        position = comma + 1;
    }
    return true;
}

void appendKeyList(std::string& line, const std::bitset<kInputKeyCount>& keys) {
    line += " keys=";
    bool first = true;
    for (int key = 0; key < kInputKeyCount; ++key) {
        if (!keys[static_cast<size_t>(key)]) continue;
        if (!first) line += ',';
        line += std::to_string(key);
        first = false;
    }
    if (first) line += '-';
}

std::string describeTickChanges(const ReplayTick& tick, const InputState& previousInput, float previousDeltaTime) {
    std::string line;
    if (tick.dt != previousDeltaTime) {
        line += " dt=";
        appendFloat(line, tick.dt);
    }
    if (tick.input.keys != previousInput.keys) appendKeyList(line, tick.input.keys);
    if (tick.input.buttons != previousInput.buttons) line += " buttons=" + std::to_string(tick.input.buttons.to_ulong());
    if (tick.input.mouse != previousInput.mouse) {
        line += " mouse=";
        appendVec2(line, tick.input.mouse);
    }
    if (tick.input.mouseDelta != glm::vec2(0.0f)) {
        line += " delta=";
        appendVec2(line, tick.input.mouseDelta);
    }
    if (tick.input.scroll != glm::vec2(0.0f)) {
        line += " scroll=";
        appendVec2(line, tick.input.scroll);
    }
    return line;
}

std::string applyField(const std::string& key, std::string_view value, InputState& state, float& deltaTime) {
    if (key == "dt") return parseFloat(value, deltaTime) ? std::string() : "bad dt";
    if (key == "keys") return parseKeyList(value, state.keys) ? std::string() : "bad key";
    if (key == "buttons") {
        unsigned long bits = 0;
        if (!parseInt(value, bits)) return "bad buttons";
        state.buttons = std::bitset<kInputMouseButtons>(bits);
        return {};
    }
    if (key == "mouse") return parseVec2(value, state.mouse) ? std::string() : "bad mouse";
    if (key == "delta") return parseVec2(value, state.mouseDelta) ? std::string() : "bad delta";
    if (key == "scroll") return parseVec2(value, state.scroll) ? std::string() : "bad scroll";
    return "unknown field: " + key;
}

std::string applyEvent(const ReplayEvent& event, InputState& state, float& deltaTime) {
    std::istringstream words(event.fields);
    std::string word;
    while (words >> word) {
        const size_t equals = word.find('=');
        if (equals == std::string::npos) return "bad field: " + word;
        const std::string message = applyField(word.substr(0, equals), std::string_view(word).substr(equals + 1), state, deltaTime);
        if (!message.empty()) return message;
    }
    return {};
}
}

void InputRecorder::begin(uint64_t seed) {
    m_data = {};
    m_data.seed = seed;
    m_active = true;
}

void InputRecorder::record(float dt, const InputState& input) {
    if (!m_active) return;
    m_data.ticks.push_back({dt, input});
}

std::string serializeReplay(const ReplayData& data) {
    std::string out = "litreplay 1\nseed " + std::to_string(data.seed) + "\nticks " + std::to_string(data.ticks.size()) + "\n";
    InputState previousInput;
    float previousDeltaTime = -1.0f;
    for (size_t index = 0; index < data.ticks.size(); ++index) {
        const ReplayTick& tick = data.ticks[index];
        const std::string line = describeTickChanges(tick, previousInput, previousDeltaTime);
        if (!line.empty()) out += "@" + std::to_string(index) + line + "\n";
        previousInput = tick.input;
        previousDeltaTime = tick.dt;
    }
    if (data.hashInterval > 0) out += "hashinterval " + std::to_string(data.hashInterval) + "\n";
    for (const ReplayCheckpoint& checkpoint : data.checkpoints) {
        char hex[kFloatBufferSize];
        std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(checkpoint.hash));
        out += "hash " + std::to_string(checkpoint.tick) + " " + hex + "\n";
    }
    out += "end\n";
    return out;
}

bool InputRecorder::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << serializeReplay(m_data);
    return static_cast<bool>(out);
}

bool parseReplay(const std::string& text, ReplayData& out, std::string* error) {
    const auto fail = [&](const std::string& reason) {
        if (error) *error = reason;
        return false;
    };
    std::istringstream input(text);
    std::string line;
    if (!std::getline(input, line) || line != "litreplay 1") return fail("bad header");
    out = {};
    size_t count = 0;
    bool haveCount = false;
    std::vector<ReplayEvent> events;
    bool ended = false;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line == "end") {
            ended = true;
            break;
        }
        if (line.rfind("seed ", 0) == 0) {
            if (!parseInt(std::string_view(line).substr(kSeedPrefixLength), out.seed)) return fail("bad seed");
        } else if (line.rfind("ticks ", 0) == 0) {
            if (!parseInt(std::string_view(line).substr(kTicksPrefixLength), count)) return fail("bad tick count");
            haveCount = true;
        } else if (line.rfind("hashinterval ", 0) == 0) {
            if (!parseInt(std::string_view(line).substr(kHashIntervalPrefixLength), out.hashInterval)) return fail("bad hash interval");
        } else if (line.rfind("hash ", 0) == 0) {
            const size_t space = line.find(' ', kHashPrefixLength);
            ReplayCheckpoint checkpoint;
            if (space == std::string::npos || !parseInt(std::string_view(line).substr(kHashPrefixLength, space - kHashPrefixLength), checkpoint.tick)) return fail("bad hash line: " + line);
            const std::string_view hex = std::string_view(line).substr(space + 1);
            const auto result = std::from_chars(hex.data(), hex.data() + hex.size(), checkpoint.hash, kHexBase);
            if (result.ec != std::errc{} || result.ptr != hex.data() + hex.size()) return fail("bad hash line: " + line);
            out.checkpoints.push_back(checkpoint);
        } else if (line[0] == '@') {
            const size_t space = line.find(' ');
            ReplayEvent event;
            if (space == std::string::npos || !parseInt(std::string_view(line).substr(1, space - 1), event.tick)) return fail("bad event line: " + line);
            event.fields = line.substr(space + 1);
            events.push_back(std::move(event));
        } else {
            return fail("unknown line: " + line);
        }
    }
    if (!haveCount || !ended) return fail("truncated replay");
    if (count > kMaxReplayTicks) return fail("tick count too large");
    out.ticks.assign(count, {});
    size_t eventIndex = 0;
    InputState state;
    float deltaTime = 0.0f;
    for (size_t tick = 0; tick < count; ++tick) {
        state.mouseDelta = glm::vec2(0.0f);
        state.scroll = glm::vec2(0.0f);
        if (eventIndex < events.size() && events[eventIndex].tick == tick) {
            const std::string message = applyEvent(events[eventIndex], state, deltaTime);
            if (!message.empty()) return fail(message);
            ++eventIndex;
        }
        out.ticks[tick] = {deltaTime, state};
    }
    if (eventIndex != events.size()) return fail("event beyond tick count");
    return true;
}

bool loadReplay(const std::string& path, ReplayData& out, std::string* error) {
    const std::optional<std::string> text = readTextFile(path);
    if (!text) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    return parseReplay(*text, out, error);
}

void ReplayVerifier::begin(const ReplayData& data) {
    m_expected = data.checkpoints;
    m_mismatches.clear();
    m_checked = 0;
}

bool ReplayVerifier::due(uint64_t tick) const {
    for (const ReplayCheckpoint& checkpoint : m_expected) {
        if (checkpoint.tick == tick) return true;
    }
    return false;
}

void ReplayVerifier::check(uint64_t tick, uint64_t hash) {
    for (const ReplayCheckpoint& checkpoint : m_expected) {
        if (checkpoint.tick != tick) continue;
        ++m_checked;
        if (checkpoint.hash != hash) m_mismatches.push_back({tick, checkpoint.hash, hash});
        return;
    }
}
