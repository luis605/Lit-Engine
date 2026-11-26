#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <mutex>
#include <optional>
#include <thread>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Engine/Bent/Check.hpp"

namespace bent {

const char* caseStatusName(CaseStatus status) {
    switch (status) {
    case CaseStatus::Pass: return "pass";
    case CaseStatus::Fail: return "fail";
    case CaseStatus::Discard: return "discard";
    case CaseStatus::Error: return "error";
    }
    return "?";
}

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

uint64_t mix64(uint64_t z) {
    z += 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

uint64_t fnv(const std::string& text) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (const unsigned char c : text) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

thread_local const char* tCurrentLaw = nullptr;
thread_local int64_t tCurrentCase = -1;
thread_local uint64_t tCurrentSeed = 0;

struct CaseMarker {
    CaseMarker(const LawDecl& law, int64_t index, uint64_t seed) {
        tCurrentLaw = law.name.c_str();
        tCurrentCase = index;
        tCurrentSeed = seed;
    }
    ~CaseMarker() {
        tCurrentLaw = nullptr;
        tCurrentCase = -1;
    }
};

int defaultJobs(int jobs) {
    if (jobs > 0) return jobs;
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 0 ? static_cast<int>(hw) : 4;
}

std::string errorClass(const CaseOutcome& outcome) {
    if (outcome.status != CaseStatus::Error) return {};
    std::string out;
    // Skip the location prefix and digits so that "same failure" survives shrinking.
    std::string message = outcome.message;
    const size_t colon = message.find(": ");
    if (colon != std::string::npos && colon < 120 && message.rfind(".bent", colon) != std::string::npos) message = message.substr(colon + 2);
    for (const char c : message) {
        if (!std::isdigit(static_cast<unsigned char>(c))) out += c;
        if (out.size() >= 48) break;
    }
    return out;
}

bool sameFailure(const CaseOutcome& reference, const CaseOutcome& candidate) {
    if (candidate.status != reference.status) return false;
    if (reference.status == CaseStatus::Error) return errorClass(reference) == errorClass(candidate);
    return true;
}

bool shortlexLess(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    if (a.size() != b.size()) return a.size() < b.size();
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

// ---------------------------------------------------------------- process isolation

void writeAll(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t written = ::write(fd, bytes, size);
        if (written <= 0) return;
        bytes += written;
        size -= static_cast<size_t>(written);
    }
}

void writeU64(std::string& out, uint64_t value) { out.append(reinterpret_cast<const char*>(&value), sizeof value); }

std::string encodeTape(char tag, const std::vector<uint64_t>& tape, const std::vector<TapeSpan>& spans) {
    std::string out(1, tag);
    writeU64(out, tape.size());
    for (const uint64_t v : tape) writeU64(out, v);
    writeU64(out, spans.size());
    for (const TapeSpan& span : spans) {
        writeU64(out, span.begin);
        writeU64(out, span.end);
        writeU64(out, span.listElement ? 1 : 0);
    }
    return out;
}

std::string encodeOutcome(const CaseOutcome& outcome) {
    std::string out = encodeTape('R', outcome.tape, outcome.spans);
    out += static_cast<char>(outcome.status);
    writeU64(out, outcome.message.size());
    out += outcome.message;
    return out;
}

struct Reader {
    const std::string& data;
    size_t pos = 0;
    bool ok = true;
    uint64_t u64() {
        if (pos + 8 > data.size()) {
            ok = false;
            return 0;
        }
        uint64_t value = 0;
        std::memcpy(&value, data.data() + pos, 8);
        pos += 8;
        return value;
    }
    void tape(std::vector<uint64_t>& tape, std::vector<TapeSpan>& spans) {
        const uint64_t n = u64();
        if (!ok || n > data.size()) {
            ok = false;
            return;
        }
        tape.resize(n);
        for (uint64_t& v : tape) v = u64();
        const uint64_t m = u64();
        if (!ok || m > data.size()) {
            ok = false;
            return;
        }
        spans.resize(m);
        for (TapeSpan& span : spans) {
            span.begin = u64();
            span.end = u64();
            span.listElement = u64() != 0;
        }
    }
};

std::string gSanitizerLogPrefix;

// Sanitizer reports of a dead child land in <prefix>.<pid> (ASAN_OPTIONS/UBSAN_OPTIONS log_path); the first lines name the bug.
std::string sanitizerReport(pid_t pid) {
    if (gSanitizerLogPrefix.empty()) return {};
    const std::string path = gSanitizerLogPrefix + "." + std::to_string(pid);
    std::ifstream in(path);
    std::string text;
    std::string line;
    int lines = 0;
    while (lines < 14 && std::getline(in, line)) {
        if (line.empty() || line.find("====") != std::string::npos) continue;
        text += "\n    " + line;
        ++lines;
    }
    in.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return text.empty() ? text : "memory-safety report:" + text;
}

CaseOutcome decodeChild(const std::string& data, int waitStatus, bool timedOut, double timeout) {
    CaseOutcome outcome;
    Reader reader{data};
    bool haveResult = false;
    while (reader.ok && reader.pos < data.size()) {
        const char tag = data[reader.pos++];
        if (tag == 'G') {
            reader.tape(outcome.tape, outcome.spans);
        } else if (tag == 'R') {
            outcome.tape.clear();
            outcome.spans.clear();
            reader.tape(outcome.tape, outcome.spans);
            if (reader.pos >= data.size()) break;
            outcome.status = static_cast<CaseStatus>(data[reader.pos++]);
            const uint64_t length = reader.u64();
            if (!reader.ok || reader.pos + length > data.size()) break;
            outcome.message = data.substr(reader.pos, length);
            reader.pos += length;
            haveResult = true;
        } else {
            break;
        }
    }
    if (haveResult) return outcome;
    outcome.status = CaseStatus::Error;
    if (timedOut) outcome.message = "case timed out after " + std::to_string(static_cast<int>(timeout)) + " s (hang)";
    else if (WIFSIGNALED(waitStatus)) outcome.message = std::string("engine crashed: signal ") + std::to_string(WTERMSIG(waitStatus)) + " (" + strsignal(WTERMSIG(waitStatus)) + ")";
    else outcome.message = "child exited with status " + std::to_string(WEXITSTATUS(waitStatus)) + " without a result";
    return outcome;
}

// Runs fn(index) for each job in a forked child, at most `parallel` at a time. The parent is single threaded while forking.
void runForked(size_t count, int parallel, double timeout, const std::function<CaseOutcome(size_t, const std::function<void(const Tape&)>&)>& fn,
               const std::function<bool(size_t, CaseOutcome&&)>& done) {
    struct Child {
        pid_t pid;
        int fd;
        size_t index;
        std::string data;
        Clock::time_point start;
    };
    std::vector<Child> running;
    size_t next = 0;
    bool stop = false;
    while ((next < count && !stop) || !running.empty()) {
        while (!stop && next < count && static_cast<int>(running.size()) < parallel) {
            int fds[2];
            if (::pipe(fds) != 0) throw std::runtime_error("pipe failed");
            std::fflush(stdout);
            std::fflush(stderr);
            const pid_t pid = ::fork();
            if (pid < 0) throw std::runtime_error("fork failed");
            if (pid == 0) {
                ::close(fds[0]);
                const int out = fds[1];
                const CaseOutcome outcome = fn(next, [out](const Tape& tape) {
                    const std::string blob = encodeTape('G', tape.drawn(), tape.spans());
                    writeAll(out, blob.data(), blob.size());
                });
                const std::string blob = encodeOutcome(outcome);
                writeAll(out, blob.data(), blob.size());
                ::close(out);
                ::_exit(0);
            }
            ::close(fds[1]);
            running.push_back({pid, fds[0], next, {}, Clock::now()});
            ++next;
        }
        std::vector<pollfd> polls;
        for (const Child& child : running) polls.push_back({child.fd, POLLIN, 0});
        ::poll(polls.data(), polls.size(), 50);
        for (size_t i = 0; i < running.size();) {
            Child& child = running[i];
            bool finished = false;
            bool timedOut = false;
            if (polls[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char buffer[65536];
                const ssize_t got = ::read(child.fd, buffer, sizeof buffer);
                if (got > 0) child.data.append(buffer, static_cast<size_t>(got));
                else finished = true;
            }
            if (!finished && secondsSince(child.start) > timeout) {
                ::kill(child.pid, SIGKILL);
                finished = true;
                timedOut = true;
            }
            if (!finished) {
                ++i;
                continue;
            }
            ::close(child.fd);
            int status = 0;
            ::waitpid(child.pid, &status, 0);
            CaseOutcome outcome = decodeChild(child.data, status, timedOut, timeout);
            if (outcome.status == CaseStatus::Error && !timedOut && !gSanitizerLogPrefix.empty()) {
                const std::string report = sanitizerReport(child.pid);
                if (!report.empty()) outcome.message = report;
            }
            const size_t index = child.index;
            running.erase(running.begin() + static_cast<long>(i));
            polls.erase(polls.begin() + static_cast<long>(i));
            if (!done(index, std::move(outcome))) stop = true;
        }
    }
}

// ---------------------------------------------------------------- case evaluation

CaseOutcome evaluateLaw(const Program& program, const LawDecl& law, Interp& in, const std::function<EnvPtr(Interp&, NamedArgs&)>& bindVars,
                        const std::function<void()>& generated, Clock::time_point start) {
    CaseOutcome outcome;
    try {
        EnvPtr env = bindVars(in, outcome.bindings);
        if (generated) generated();
        bool accepted = true;
        if (law.where) accepted = truthy(in.eval(law.where, env), law.where->loc);
        if (!accepted) {
            outcome.status = CaseStatus::Discard;
            outcome.message = "precondition is false";
        } else {
            const Value result = in.eval(law.property, env);
            outcome.status = truthy(result, law.property->loc) ? CaseStatus::Pass : CaseStatus::Fail;
            if (outcome.status == CaseStatus::Fail) outcome.message = "property is false";
        }
    } catch (const Discard& discard) {
        outcome.status = CaseStatus::Discard;
        outcome.message = discard.reason;
    } catch (const Overrun&) {
        outcome.status = CaseStatus::Discard;
        outcome.message = "generator drew too many choices";
    } catch (const BentError& error) {
        outcome.status = CaseStatus::Error;
        outcome.message = error.what();
    } catch (const std::exception& error) {
        outcome.status = CaseStatus::Error;
        outcome.message = std::string("exception: ") + error.what();
    } catch (...) {
        outcome.status = CaseStatus::Error;
        outcome.message = "unknown exception";
    }
    (void)program;
    outcome.notes = std::move(in.notes);
    outcome.covers = std::move(in.covers);
    outcome.millis = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return outcome;
}

}

uint64_t caseSeed(uint64_t seed, const std::string& law, int64_t caseIndex) { return mix64(mix64(seed ^ fnv(law)) + static_cast<uint64_t>(caseIndex) * 0x9e3779b97f4a7c15ULL); }

double caseSize(int64_t caseIndex) { return std::min(1.0, static_cast<double>(caseIndex + 1) / 40.0); }

namespace {
CaseOutcome runCaseWithHook(const Program& program, const LawDecl& law, Tape& tape, bool explain, double timeLimit, const std::function<void(const Tape&)>& generatedHook) {
    const Clock::time_point start = Clock::now();
    Interp in(program);
    in.tape = &tape;
    in.explain = explain;
    if (timeLimit > 0.0) in.deadline = start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timeLimit));
    CaseOutcome outcome = evaluateLaw(
        program, law, in,
        [&](Interp& interp, NamedArgs& bindings) {
            EnvPtr env;
            for (const auto& [name, genExpr] : law.vars) {
                const Value gen = interp.eval(genExpr, env);
                Value value = interp.realize(gen, genExpr->loc);
                bindings.emplace_back(name, value);
                env = bind(env, name, std::move(value));
            }
            return env;
        },
        [&]() {
            if (generatedHook) generatedHook(tape);
        },
        start);
    outcome.tape = tape.drawn();
    outcome.spans = tape.spans();
    return outcome;
}
}

CaseOutcome runCase(const Program& program, const LawDecl& law, Tape& tape, bool explain, double timeLimit) { return runCaseWithHook(program, law, tape, explain, timeLimit, {}); }

CaseOutcome runExample(const Program& program, const ExampleDecl& example, bool explain, double timeLimit) {
    const LawDecl* law = program.law(example.law);
    if (!law) {
        CaseOutcome outcome;
        outcome.status = CaseStatus::Error;
        outcome.message = example.loc.str() + ": example for unknown law '" + example.law + "'";
        return outcome;
    }
    const Clock::time_point start = Clock::now();
    Interp in(program);
    in.explain = explain;
    if (timeLimit > 0.0) in.deadline = start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timeLimit));
    Tape tape = Tape::replay({});
    in.tape = &tape;  // generators used inside the property read the all-zero tape (simplest values)
    return evaluateLaw(
        program, *law, in,
        [&](Interp& interp, NamedArgs& bindings) {
            EnvPtr env;
            for (const auto& [name, genExpr] : law->vars) {
                const auto it = std::find_if(example.bindings.begin(), example.bindings.end(), [&](const auto& b) { return b.first == name; });
                if (it == example.bindings.end()) throw BentError(example.loc, "example for '" + law->name + "' does not bind '" + name + "'");
                Value value = interp.eval(it->second, env);
                if (containsGen(value)) value = interp.realize(value, it->second->loc);
                bindings.emplace_back(name, value);
                env = bind(env, name, std::move(value));
            }
            for (const auto& binding : example.bindings) {
                if (std::none_of(law->vars.begin(), law->vars.end(), [&](const auto& v) { return v.first == binding.first; })) throw BentError(binding.second->loc, "law '" + law->name + "' has no variable '" + binding.first + "'");
            }
            return env;
        },
        {}, start);
}

std::vector<const LawDecl*> selectLaws(const Program& program, const std::vector<std::string>& filter, const std::string& topFile) {
    std::vector<const LawDecl*> out;
    for (const auto& law : program.laws) {
        if (!filter.empty()) {
            if (std::find(filter.begin(), filter.end(), law->name) != filter.end()) out.push_back(law.get());
        } else if (topFile.empty() || law->file == topFile) {
            out.push_back(law.get());
        }
    }
    return out;
}

// ---------------------------------------------------------------- shrinking

namespace {

class Shrinker {
  public:
    using Test = std::function<std::vector<CaseOutcome>(const std::vector<std::vector<uint64_t>>&)>;

    Shrinker(CaseOutcome initial, Test test, int64_t limit, double seconds) : m_best(std::move(initial)), m_test(std::move(test)), m_limit(limit), m_seconds(seconds), m_start(Clock::now()) {}

    void run() {
        tryCandidates({std::vector<uint64_t>{}});  // the all-simplest case first
        bool progress = true;
        while (progress && !exhausted()) {
            progress = false;
            progress |= deleteListElements();
            progress |= zeroSpans();
            progress |= deleteBlocks();
            progress |= minimizeValues();
        }
    }

    [[nodiscard]] const CaseOutcome& best() const { return m_best; }
    [[nodiscard]] int64_t attempts() const { return m_attempts; }
    [[nodiscard]] int64_t accepted() const { return m_accepted; }

  private:
    CaseOutcome m_best;
    Test m_test;
    int64_t m_limit;
    double m_seconds;
    Clock::time_point m_start;
    int64_t m_attempts = 0;
    int64_t m_accepted = 0;

    bool exhausted() const { return m_attempts >= m_limit || secondsSince(m_start) > m_seconds; }

    // Evaluates candidates in order (in parallel batches); accepts the first one that still fails the same way and is smaller.
    bool tryCandidates(std::vector<std::vector<uint64_t>> candidates) {
        std::erase_if(candidates, [&](const auto& c) { return !shortlexLess(c, m_best.tape); });
        if (candidates.empty() || exhausted()) return false;
        const int64_t room = m_limit - m_attempts;
        if (static_cast<int64_t>(candidates.size()) > room) candidates.resize(static_cast<size_t>(std::max<int64_t>(room, 0)));
        const std::vector<CaseOutcome> outcomes = m_test(candidates);
        m_attempts += static_cast<int64_t>(outcomes.size());
        for (const CaseOutcome& outcome : outcomes) {
            if (sameFailure(m_best, outcome) && shortlexLess(outcome.tape, m_best.tape)) {
                m_best = outcome;
                ++m_accepted;
                return true;
            }
        }
        return false;
    }

    // Batches candidate generation so a pass restarts from the improved tape after every success.
    template <typename Make>
    bool pass(Make&& make, size_t batch) {
        bool any = false;
        size_t cursor = 0;
        while (!exhausted()) {
            std::vector<std::vector<uint64_t>> candidates;
            size_t scanned = cursor;
            while (candidates.size() < batch) {
                std::optional<std::vector<uint64_t>> candidate = make(scanned);
                if (!candidate) break;
                ++scanned;
                if (shortlexLess(*candidate, m_best.tape)) candidates.push_back(std::move(*candidate));
            }
            if (candidates.empty()) break;
            if (tryCandidates(candidates)) {
                any = true;
                continue;  // same cursor: indices refer to the new tape
            }
            cursor = scanned;
        }
        return any;
    }

    bool deleteListElements() {
        return pass(
            [&](size_t index) -> std::optional<std::vector<uint64_t>> {
                std::vector<TapeSpan> spans;
                for (const TapeSpan& span : m_best.spans) {
                    if (span.listElement) spans.push_back(span);
                }
                // Larger elements and later elements first.
                std::stable_sort(spans.begin(), spans.end(), [](const TapeSpan& a, const TapeSpan& b) { return a.begin > b.begin; });
                if (index >= spans.size()) return std::nullopt;
                std::vector<uint64_t> tape = m_best.tape;
                const TapeSpan& span = spans[index];
                if (span.end > tape.size()) return m_best.tape;
                tape.erase(tape.begin() + static_cast<long>(span.begin), tape.begin() + static_cast<long>(span.end));
                return tape;
            },
            8);
    }

    bool zeroSpans() {
        return pass(
            [&](size_t index) -> std::optional<std::vector<uint64_t>> {
                std::vector<TapeSpan> spans = m_best.spans;
                std::stable_sort(spans.begin(), spans.end(), [](const TapeSpan& a, const TapeSpan& b) { return (a.end - a.begin) > (b.end - b.begin); });
                while (index < spans.size()) {
                    const TapeSpan& span = spans[index];
                    if (span.end <= m_best.tape.size() && std::any_of(m_best.tape.begin() + static_cast<long>(span.begin), m_best.tape.begin() + static_cast<long>(span.end), [](uint64_t v) { return v != 0; })) {
                        std::vector<uint64_t> tape = m_best.tape;
                        std::fill(tape.begin() + static_cast<long>(span.begin), tape.begin() + static_cast<long>(span.end), 0);
                        return tape;
                    }
                    return m_best.tape;  // nothing to zero: filtered as not smaller
                }
                return std::nullopt;
            },
            8);
    }

    bool deleteBlocks() {
        bool any = false;
        for (const size_t width : {8u, 4u, 2u, 1u}) {
            any |= pass(
                [&, width](size_t index) -> std::optional<std::vector<uint64_t>> {
                    const size_t n = m_best.tape.size();
                    if (n < width || index > n - width) return std::nullopt;
                    const size_t at = n - width - index;
                    std::vector<uint64_t> tape = m_best.tape;
                    tape.erase(tape.begin() + static_cast<long>(at), tape.begin() + static_cast<long>(at + width));
                    return tape;
                },
                8);
        }
        return any;
    }

    bool minimizeValues() {
        bool any = false;
        for (size_t at = 0; at < m_best.tape.size() && !exhausted(); ++at) {
            const uint64_t value = m_best.tape[at];
            if (value == 0) continue;
            std::vector<uint64_t> values = {0};
            for (int shift = 32; shift >= 1; shift /= 2) {
                if ((value >> shift) > 0) values.push_back(value >> shift);
            }
            values.push_back(value - 1);
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            std::erase_if(values, [&](uint64_t v) { return v >= value; });
            std::vector<std::vector<uint64_t>> candidates;
            for (const uint64_t v : values) {
                std::vector<uint64_t> tape = m_best.tape;
                tape[at] = v;
                candidates.push_back(std::move(tape));
            }
            if (tryCandidates(std::move(candidates))) {
                any = true;
                // Binary search between the accepted value and the next larger rejected one.
                if (at < m_best.tape.size()) {
                    uint64_t lo = 0;
                    uint64_t hi = m_best.tape[at];
                    while (hi > lo + 1 && !exhausted()) {
                        const uint64_t mid = lo + (hi - lo) / 2;
                        std::vector<uint64_t> tape = m_best.tape;
                        tape[at] = mid;
                        if (tryCandidates({tape})) hi = m_best.tape.size() > at ? m_best.tape[at] : mid;
                        else lo = mid;
                    }
                }
            }
        }
        return any;
    }
};

std::vector<CaseOutcome> evaluateTapes(const Program& program, const LawDecl& law, const std::vector<std::vector<uint64_t>>& tapes, double size, const CheckOptions& options) {
    std::vector<CaseOutcome> out(tapes.size());
    const int jobs = std::min<int>(defaultJobs(options.jobs), static_cast<int>(tapes.size()));
    if (options.isolate) {
        runForked(tapes.size(), jobs, options.caseTimeout,
                  [&](size_t index, const std::function<void(const Tape&)>& hook) {
                      Tape tape = Tape::replay(tapes[index], size);
                      return runCaseWithHook(program, law, tape, false, options.caseTimeout, hook);
                  },
                  [&](size_t index, CaseOutcome&& outcome) {
                      out[index] = std::move(outcome);
                      return true;
                  });
        return out;
    }
    std::atomic<size_t> next{0};
    const auto worker = [&]() {
        while (true) {
            const size_t index = next.fetch_add(1);
            if (index >= tapes.size()) return;
            CaseMarker marker(law, -2, 0);
            Tape tape = Tape::replay(tapes[index], size);
            out[index] = runCase(program, law, tape, false, options.caseTimeout);
        }
    };
    if (jobs <= 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        for (int t = 0; t < jobs; ++t) threads.emplace_back(worker);
        for (std::thread& t : threads) t.join();
    }
    return out;
}

std::string relativeInclude(const std::string& lawFile, const std::string& regressionDir) {
    std::error_code ec;
    const std::filesystem::path law = std::filesystem::weakly_canonical(lawFile, ec);
    const std::filesystem::path dir = std::filesystem::weakly_canonical(regressionDir, ec);
    std::filesystem::path rel = std::filesystem::relative(law, dir, ec);
    if (ec || rel.empty()) return lawFile;
    return rel.generic_string();
}

void finishFailure(const Program& program, const LawDecl& law, const CheckOptions& options, LawReport& report) {
    const double size = caseSize(report.failingCase);
    if (options.shrink) {
        Shrinker shrinker(
            report.original, [&](const std::vector<std::vector<uint64_t>>& tapes) { return evaluateTapes(program, law, tapes, size, options); }, options.shrinkLimit,
            options.shrinkSeconds);
        shrinker.run();
        report.shrinkAttempts = shrinker.attempts();
        report.shrinkAccepted = shrinker.accepted();
        report.shrunk = shrinker.best();
    } else {
        report.shrunk = report.original;
    }
    // Final explained replay of the minimal tape (in a child process when isolating, the parent must survive a crash).
    if (options.isolate) {
        CaseOutcome crashed = report.shrunk;
        const std::vector<CaseOutcome> again = evaluateTapes(program, law, {report.shrunk.tape}, size, options);
        if (again.front().status == CaseStatus::Error && (again.front().message.find("crashed") != std::string::npos || again.front().message.find("memory-safety report") != std::string::npos || again.front().message.find("child exited") != std::string::npos)) {
            // Recover the bindings without running the property: generate only.
            Interp in(program);
            Tape tape = Tape::replay(report.shrunk.tape, size);
            in.tape = &tape;
            EnvPtr env;
            NamedArgs bindings;
            try {
                for (const auto& [name, genExpr] : law.vars) {
                    Value value = in.realize(in.eval(genExpr, env), genExpr->loc);
                    bindings.emplace_back(name, value);
                    env = bind(env, name, std::move(value));
                }
            } catch (...) {
            }
            report.shrunk.bindings = std::move(bindings);
            report.shrunk.message = again.front().message;
        } else {
            Tape tape = Tape::replay(report.shrunk.tape, size);
            CaseOutcome explained = runCase(program, law, tape, true, options.caseTimeout);
            if (sameFailure(crashed, explained)) report.shrunk = std::move(explained);
        }
    } else {
        Tape tape = Tape::replay(report.shrunk.tape, size);
        CaseMarker marker(law, report.failingCase, options.seed);
        CaseOutcome explained = runCase(program, law, tape, true, options.caseTimeout);
        if (sameFailure(report.shrunk, explained)) report.shrunk = std::move(explained);
    }
    report.status = report.shrunk.status == CaseStatus::Error ? "error" : "fail";
    std::vector<std::string> header;
    header.push_back("Bent regression: shrunk counterexample for law `" + law.name + "` (" + law.file + ")");
    header.push_back("Found by: " + (options.commandLine.empty() ? std::string("bent check") : options.commandLine) + " (seed " + std::to_string(options.seed) + ", case " + std::to_string(report.failingCase) + ")");
    header.push_back("Shrunk " + std::to_string(report.shrinkAccepted) + " times in " + std::to_string(report.shrinkAttempts) + " attempts; tape " + std::to_string(report.original.tape.size()) + " -> " + std::to_string(report.shrunk.tape.size()) + " choices");
    header.push_back("Replay the random case: bent replay " + law.file + " --law " + law.name + " --seed " + std::to_string(options.seed) + " --case " + std::to_string(report.failingCase));
    header.push_back("Failure: " + report.shrunk.message);
    for (const std::string& note : report.shrunk.notes) header.push_back("  " + note);
    std::string include = law.file;
    if (!options.regressionDir.empty()) include = relativeInclude(law.file, options.regressionDir);
    report.counterexample = counterexampleProgram(law, report.shrunk.bindings, include, header);
    if (!options.regressionDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(options.regressionDir, ec);
        std::string key;
        for (const auto& [name, value] : report.shrunk.bindings) key += name + "=" + print(value) + ";";
        char hex[17];
        std::snprintf(hex, sizeof hex, "%08llx", static_cast<unsigned long long>(fnv(law.name + key) & 0xffffffffULL));
        const std::filesystem::path path = std::filesystem::path(options.regressionDir) / (law.name + "-" + hex + ".bent");
        if (!std::filesystem::exists(path)) {
            std::ofstream out(path);
            out << report.counterexample;
        }
        report.regressionPath = path.string();
    }
}

}

LawReport checkLaw(const Program& program, const LawDecl& law, const CheckOptions& options, double lawBudget) {
    LawReport report;
    report.law = law.name;
    report.file = law.file;
    report.seed = options.seed;
    const Clock::time_point start = Clock::now();
    const int jobs = defaultJobs(options.jobs);
    std::atomic<int64_t> firstFail{std::numeric_limits<int64_t>::max()};
    std::atomic<int64_t> passed{0};
    std::atomic<int64_t> discards{0};
    std::atomic<int64_t> ran{0};
    std::mutex mutex;
    std::map<int64_t, CaseOutcome> failures;
    const auto overBudget = [&]() { return lawBudget > 0.0 && secondsSince(start) > lawBudget; };
    std::map<std::string, std::pair<int64_t, int64_t>> coverage;
    const auto record = [&](int64_t k, CaseOutcome&& outcome) {
        ++ran;
        if (!outcome.covers.empty() && outcome.status != CaseStatus::Discard) {
            std::lock_guard lock(mutex);
            std::set<std::string> seen;
            for (const auto& [label, held] : outcome.covers) {
                auto& entry = coverage[label];
                if (seen.insert(label).second) ++entry.second;
                if (held && seen.insert("hit:" + label).second) ++entry.first;
            }
        }
        if (outcome.status == CaseStatus::Pass) ++passed;
        else if (outcome.status == CaseStatus::Discard) ++discards;
        else {
            std::lock_guard lock(mutex);
            failures.emplace(k, std::move(outcome));
            int64_t current = firstFail.load();
            while (k < current && !firstFail.compare_exchange_weak(current, k)) {
            }
        }
    };
    if (options.isolate) {
        runForked(static_cast<size_t>(options.cases), jobs, options.caseTimeout,
                  [&](size_t index, const std::function<void(const Tape&)>& hook) {
                      const int64_t k = static_cast<int64_t>(index);
                      Tape tape = Tape::random(caseSeed(options.seed, law.name, k), caseSize(k));
                      return runCaseWithHook(program, law, tape, false, options.caseTimeout, hook);
                  },
                  [&](size_t index, CaseOutcome&& outcome) {
                      record(static_cast<int64_t>(index), std::move(outcome));
                      return firstFail.load() == std::numeric_limits<int64_t>::max() && !overBudget();
                  });
    } else {
        std::atomic<int64_t> next{0};
        const auto worker = [&]() {
            while (true) {
                const int64_t k = next.fetch_add(1);
                if (k >= options.cases || k > firstFail.load() || overBudget()) return;
                CaseMarker marker(law, k, options.seed);
                Tape tape = Tape::random(caseSeed(options.seed, law.name, k), caseSize(k));
                record(k, runCase(program, law, tape, false, options.caseTimeout));
            }
        };
        std::vector<std::thread> threads;
        for (int t = 1; t < jobs; ++t) threads.emplace_back(worker);
        worker();
        for (std::thread& t : threads) t.join();
    }
    report.cases = ran.load();
    report.passed = passed.load();
    report.discards = discards.load();
    if (firstFail.load() != std::numeric_limits<int64_t>::max()) {
        report.failingCase = firstFail.load();
        report.original = failures[report.failingCase];
        if (options.progress) options.progress("law " + law.name + ": case " + std::to_string(report.failingCase) + " " + caseStatusName(report.original.status) + " (" + truncate(report.original.message, 160) + "), shrinking...");
        finishFailure(program, law, options, report);
    } else if (report.passed == 0 && report.cases > 0) {
        report.status = "gave-up";
    } else {
        report.status = "pass";
    }
    for (const auto& [label, counts] : coverage) report.cover.emplace_back(label, counts);
    report.showCover = options.coverage;
    // Non-vacuity: a cover() label that no case hit means the law never exercised what it claims to check.
    if (report.status == "pass" && report.cases >= 20) {
        for (const auto& [label, counts] : report.cover) {
            if (counts.first == 0) report.status = "vacuous";
        }
    }
    report.seconds = secondsSince(start);
    return report;
}

LawReport shrinkCase(const Program& program, const LawDecl& law, const CheckOptions& options, int64_t caseIndex) {
    LawReport report;
    report.law = law.name;
    report.file = law.file;
    report.seed = options.seed;
    const Clock::time_point start = Clock::now();
    Tape tape = Tape::random(caseSeed(options.seed, law.name, caseIndex), caseSize(caseIndex));
    std::vector<CaseOutcome> first;
    if (options.isolate) {
        runForked(1, 1, options.caseTimeout,
                  [&](size_t, const std::function<void(const Tape&)>& hook) { return runCaseWithHook(program, law, tape, false, options.caseTimeout, hook); },
                  [&](size_t, CaseOutcome&& outcome) {
                      first.push_back(std::move(outcome));
                      return true;
                  });
    } else {
        first.push_back(runCase(program, law, tape, false, options.caseTimeout));
    }
    report.cases = 1;
    report.original = first.front();
    if (report.original.status == CaseStatus::Fail || report.original.status == CaseStatus::Error) {
        report.failingCase = caseIndex;
        finishFailure(program, law, options, report);
    } else {
        report.status = report.original.status == CaseStatus::Pass ? "pass" : "gave-up";
        report.shrunk = report.original;
    }
    report.seconds = secondsSince(start);
    return report;
}

std::vector<ExampleReport> runExamples(const Program& program, const CheckOptions& options) {
    std::vector<ExampleReport> out(program.examples.size());
    const auto evaluate = [&](size_t index) {
        const ExampleDecl& example = *program.examples[index];
        ExampleReport& report = out[index];
        report.law = example.law;
        report.file = example.file;
        report.line = example.loc.line;
        report.known = example.known;
        report.reason = example.reason;
        report.outcome = runExample(program, example, true, options.caseTimeout);
        const bool holds = report.outcome.status == CaseStatus::Pass;
        if (example.known) report.verdict = holds ? "fixed" : "known-fail";
        else report.verdict = holds ? "pass" : "fail";
    };
    if (options.isolate) {
        runForked(out.size(), defaultJobs(options.jobs), options.caseTimeout,
                  [&](size_t index, const std::function<void(const Tape&)>&) {
                      return runExample(program, *program.examples[index], false, options.caseTimeout);
                  },
                  [&](size_t index, CaseOutcome&& outcome) {
                      const ExampleDecl& example = *program.examples[index];
                      ExampleReport& report = out[index];
                      report.law = example.law;
                      report.file = example.file;
                      report.line = example.loc.line;
                      report.known = example.known;
                      report.reason = example.reason;
                      report.outcome = std::move(outcome);
                      const bool holds = report.outcome.status == CaseStatus::Pass;
                      if (example.known) report.verdict = holds ? "fixed" : "known-fail";
                      else report.verdict = holds ? "pass" : "fail";
                      return true;
                  });
        return out;
    }
    std::atomic<size_t> next{0};
    const auto worker = [&]() {
        while (true) {
            const size_t index = next.fetch_add(1);
            if (index >= out.size()) return;
            evaluate(index);
        }
    };
    std::vector<std::thread> threads;
    for (int t = 1; t < defaultJobs(options.jobs); ++t) threads.emplace_back(worker);
    worker();
    for (std::thread& t : threads) t.join();
    return out;
}

// ---------------------------------------------------------------- output

std::string prettyValue(const Value& value, int indent) {
    const std::string flat = print(value);
    if (value.kind != Kind::List || flat.size() + static_cast<size_t>(indent) < 100 || value.items().empty()) return flat;
    const std::string pad(static_cast<size_t>(indent) + 2, ' ');
    std::string out = "[\n";
    const ValueList& items = value.items();
    for (size_t index = 0; index < items.size(); ++index) {
        out += pad + prettyValue(items[index], indent + 2);
        out += index + 1 < items.size() ? ",\n" : "\n";
    }
    out += std::string(static_cast<size_t>(indent), ' ') + "]";
    return out;
}

std::string counterexampleProgram(const LawDecl& law, const NamedArgs& bindings, const std::string& includePath, const std::vector<std::string>& header) {
    std::string out;
    for (const std::string& line : header) {
        std::string text = line;
        for (char& c : text) {
            if (c == '\n') c = ' ';
        }
        out += "# " + text + "\n";
    }
    out += "include " + quoteString(includePath) + "\n\n";
    out += "example " + law.name + " {\n";
    for (size_t index = 0; index < bindings.size(); ++index) {
        out += "  " + bindings[index].first + " = " + prettyValue(bindings[index].second, 2);
        out += index + 1 < bindings.size() ? ",\n" : "\n";
    }
    out += "}\n";
    return out;
}

std::string formatReport(const LawReport& report) {
    std::string out;
    char line[512];
    std::snprintf(line, sizeof line, "%-8s %-40s %6lld cases (%lld passed, %lld discarded) %.2fs", report.status == "pass" ? "PASS" : (report.status == "gave-up" ? "GAVE-UP" : (report.status == "error" ? "ERROR" : (report.status == "vacuous" ? "VACUOUS" : "FAIL"))), report.law.c_str(),
                  static_cast<long long>(report.cases), static_cast<long long>(report.passed), static_cast<long long>(report.discards), report.seconds);
    out += line;
    out += "\n";
    if (report.showCover || report.status == "vacuous") {
        for (const auto& [label, counts] : report.cover) {
            std::snprintf(line, sizeof line, "    cover %-40s hit in %lld of %lld cases (%.1f%%)%s\n", label.c_str(), static_cast<long long>(counts.first), static_cast<long long>(counts.second),
                          counts.second > 0 ? 100.0 * static_cast<double>(counts.first) / static_cast<double>(counts.second) : 0.0, counts.first == 0 ? "  <-- never hit: the law is vacuous" : "");
            out += line;
        }
    }
    if (report.failingCase >= 0) {
        out += "  failing case " + std::to_string(report.failingCase) + " (seed " + std::to_string(report.seed) + "): " + report.original.message + "\n";
        out += "  shrunk " + std::to_string(report.shrinkAccepted) + "x in " + std::to_string(report.shrinkAttempts) + " attempts to:\n";
        for (const auto& [name, value] : report.shrunk.bindings) out += "    " + name + " = " + truncate(prettyValue(value, 4), 4000) + "\n";
        if (report.shrunk.message != report.original.message) out += "  minimal failure: " + report.shrunk.message + "\n";
        for (const std::string& note : report.shrunk.notes) out += "    | " + note + "\n";
        if (!report.regressionPath.empty()) out += "  regression written to " + report.regressionPath + "\n";
    }
    return out;
}

std::string jsonEscape(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20) {
                char hex[8];
                std::snprintf(hex, sizeof hex, "\\u%04x", c);
                out += hex;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

namespace {
std::string outcomeJson(const CaseOutcome& outcome) {
    std::string out = "{\"status\": " + jsonEscape(caseStatusName(outcome.status)) + ", \"message\": " + jsonEscape(outcome.message) + ", \"tape_length\": " + std::to_string(outcome.tape.size()) + ", \"bindings\": {";
    for (size_t index = 0; index < outcome.bindings.size(); ++index) {
        if (index) out += ", ";
        out += jsonEscape(outcome.bindings[index].first) + ": " + jsonEscape(print(outcome.bindings[index].second));
    }
    out += "}, \"notes\": [";
    for (size_t index = 0; index < outcome.notes.size(); ++index) {
        if (index) out += ", ";
        out += jsonEscape(outcome.notes[index]);
    }
    return out + "]}";
}
}

std::string reportsJson(const std::vector<LawReport>& laws, const std::vector<ExampleReport>& examples, const CheckOptions& options) {
    std::string out = "{\n  \"seed\": " + std::to_string(options.seed) + ",\n  \"cases\": " + std::to_string(options.cases) + ",\n  \"laws\": [";
    for (size_t index = 0; index < laws.size(); ++index) {
        const LawReport& r = laws[index];
        out += index ? ",\n    " : "\n    ";
        out += "{\"law\": " + jsonEscape(r.law) + ", \"file\": " + jsonEscape(r.file) + ", \"status\": " + jsonEscape(r.status) + ", \"cases\": " + std::to_string(r.cases) + ", \"passed\": " + std::to_string(r.passed) +
               ", \"discarded\": " + std::to_string(r.discards) + ", \"seconds\": " + printFloat(r.seconds);
        if (r.failingCase >= 0) {
            out += ", \"failing_case\": " + std::to_string(r.failingCase) + ", \"shrink_attempts\": " + std::to_string(r.shrinkAttempts) + ", \"shrink_steps\": " + std::to_string(r.shrinkAccepted) + ", \"original\": " + outcomeJson(r.original) +
                   ", \"shrunk\": " + outcomeJson(r.shrunk) + ", \"counterexample\": " + jsonEscape(r.counterexample) + ", \"regression\": " + jsonEscape(r.regressionPath);
        }
        out += "}";
    }
    out += laws.empty() ? "],\n" : "\n  ],\n";
    out += "  \"examples\": [";
    for (size_t index = 0; index < examples.size(); ++index) {
        const ExampleReport& e = examples[index];
        out += index ? ",\n    " : "\n    ";
        out += "{\"law\": " + jsonEscape(e.law) + ", \"file\": " + jsonEscape(e.file) + ", \"line\": " + std::to_string(e.line) + ", \"known\": " + (e.known ? "true" : "false") + ", \"verdict\": " + jsonEscape(e.verdict) +
               ", \"outcome\": " + outcomeJson(e.outcome) + "}";
    }
    out += examples.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

// ---------------------------------------------------------------- crash reporter

namespace {
void crashHandler(int sig) {
    char buffer[512];
    int length = 0;
    if (tCurrentLaw) {
        if (tCurrentCase >= 0) length = std::snprintf(buffer, sizeof buffer, "\nbent: %s while running law '%s' case %lld (seed %llu).\nbent: rerun with --isolate to survive the crash and shrink it: bent check FILE --law %s --seed %llu --isolate\n", strsignal(sig), tCurrentLaw, static_cast<long long>(tCurrentCase), static_cast<unsigned long long>(tCurrentSeed), tCurrentLaw, static_cast<unsigned long long>(tCurrentSeed));
        else length = std::snprintf(buffer, sizeof buffer, "\nbent: %s while shrinking law '%s'; rerun with --isolate.\n", strsignal(sig), tCurrentLaw);
    } else {
        length = std::snprintf(buffer, sizeof buffer, "\nbent: %s\n", strsignal(sig));
    }
    if (length > 0) writeAll(2, buffer, static_cast<size_t>(std::min<int>(length, sizeof buffer)));
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
}

void installCrashReporter() {
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE, crashHandler);
    std::signal(SIGBUS, crashHandler);
    std::signal(SIGILL, crashHandler);
}

void setSanitizerLogPrefix(const std::string& prefix) { gSanitizerLogPrefix = prefix; }

}
