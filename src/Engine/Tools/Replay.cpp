module;

#include <bitset>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

module Engine.Replay;

namespace {

void appendFloat(std::string& out, float v) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, r.ptr);
}

void appendVec2(std::string& out, const glm::vec2& v) {
    appendFloat(out, v.x);
    out += ',';
    appendFloat(out, v.y);
}

bool parseFloat(std::string_view s, float& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

bool parseVec2(std::string_view s, glm::vec2& out) {
    const size_t comma = s.find(',');
    if (comma == std::string_view::npos) return false;
    return parseFloat(s.substr(0, comma), out.x) && parseFloat(s.substr(comma + 1), out.y);
}

template <typename T>
bool parseInt(std::string_view s, T& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

}  // namespace

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
    InputState prev;
    float prevDt = -1.0f;
    for (size_t i = 0; i < data.ticks.size(); ++i) {
        const ReplayTick& t = data.ticks[i];
        std::string line;
        if (t.dt != prevDt) {
            line += " dt=";
            appendFloat(line, t.dt);
        }
        if (t.input.keys != prev.keys) {
            line += " keys=";
            bool first = true;
            for (int k = 0; k < kInputKeyCount; ++k) {
                if (!t.input.keys[static_cast<size_t>(k)]) continue;
                if (!first) line += ',';
                line += std::to_string(k);
                first = false;
            }
            if (first) line += '-';
        }
        if (t.input.buttons != prev.buttons) line += " buttons=" + std::to_string(t.input.buttons.to_ulong());
        if (t.input.mouse != prev.mouse) {
            line += " mouse=";
            appendVec2(line, t.input.mouse);
        }
        if (t.input.mouseDelta != glm::vec2(0.0f)) {
            line += " delta=";
            appendVec2(line, t.input.mouseDelta);
        }
        if (t.input.scroll != glm::vec2(0.0f)) {
            line += " scroll=";
            appendVec2(line, t.input.scroll);
        }
        if (!line.empty()) out += "@" + std::to_string(i) + line + "\n";
        prev = t.input;
        prevDt = t.dt;
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
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "litreplay 1") return fail("bad header");
    out = {};
    size_t count = 0;
    bool haveCount = false;
    struct Event {
        size_t tick;
        std::string rest;
    };
    std::vector<Event> events;
    bool ended = false;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line == "end") {
            ended = true;
            break;
        }
        if (line.rfind("seed ", 0) == 0) {
            if (!parseInt(std::string_view(line).substr(5), out.seed)) return fail("bad seed");
        } else if (line.rfind("ticks ", 0) == 0) {
            if (!parseInt(std::string_view(line).substr(6), count)) return fail("bad tick count");
            haveCount = true;
        } else if (line[0] == '@') {
            const size_t space = line.find(' ');
            Event e;
            if (space == std::string::npos || !parseInt(std::string_view(line).substr(1, space - 1), e.tick)) return fail("bad event line: " + line);
            e.rest = line.substr(space + 1);
            events.push_back(std::move(e));
        } else {
            return fail("unknown line: " + line);
        }
    }
    if (!haveCount || !ended) return fail("truncated replay");
    if (count > 100000000) return fail("tick count too large");
    out.ticks.assign(count, {});
    size_t ei = 0;
    InputState state;
    float dt = 0.0f;
    for (size_t t = 0; t < count; ++t) {
        state.mouseDelta = glm::vec2(0.0f);
        state.scroll = glm::vec2(0.0f);
        if (ei < events.size() && events[ei].tick == t) {
            std::istringstream words(events[ei].rest);
            std::string word;
            while (words >> word) {
                const size_t eq = word.find('=');
                if (eq == std::string::npos) return fail("bad field: " + word);
                const std::string key = word.substr(0, eq);
                const std::string_view value = std::string_view(word).substr(eq + 1);
                if (key == "dt") {
                    if (!parseFloat(value, dt)) return fail("bad dt");
                } else if (key == "keys") {
                    state.keys.reset();
                    if (value != "-") {
                        size_t pos = 0;
                        while (pos <= value.size()) {
                            size_t comma = value.find(',', pos);
                            if (comma == std::string_view::npos) comma = value.size();
                            int k = 0;
                            if (!parseInt(value.substr(pos, comma - pos), k) || k < 0 || k >= kInputKeyCount) return fail("bad key");
                            state.keys.set(static_cast<size_t>(k));
                            pos = comma + 1;
                        }
                    }
                } else if (key == "buttons") {
                    unsigned long bits = 0;
                    if (!parseInt(value, bits)) return fail("bad buttons");
                    state.buttons = std::bitset<kInputMouseButtons>(bits);
                } else if (key == "mouse") {
                    if (!parseVec2(value, state.mouse)) return fail("bad mouse");
                } else if (key == "delta") {
                    if (!parseVec2(value, state.mouseDelta)) return fail("bad delta");
                } else if (key == "scroll") {
                    if (!parseVec2(value, state.scroll)) return fail("bad scroll");
                } else {
                    return fail("unknown field: " + key);
                }
            }
            ++ei;
        }
        out.ticks[t] = {dt, state};
    }
    if (ei != events.size()) return fail("event beyond tick count");
    return true;
}

bool loadReplay(const std::string& path, ReplayData& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parseReplay(buffer.str(), out, error);
}
