#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Engine/Bent/Ast.hpp"
#include "Engine/Bent/Value.hpp"

namespace bent {

using NamedArgs = std::vector<std::pair<std::string, Value>>;

// ---------------------------------------------------------------- environments

struct Env {
    std::string name;
    Value value;
    std::shared_ptr<const Env> parent;
};
using EnvPtr = std::shared_ptr<const Env>;

[[nodiscard]] EnvPtr bind(EnvPtr parent, std::string name, Value value);

// ---------------------------------------------------------------- choice tape

// Generators never call a random number generator directly: they draw bounded choices from a tape. A fresh case fills the tape from a
// seeded PRNG; a replay or shrink attempt reads a recorded tape (missing choices read as 0, too large ones are clamped). Every generator
// maps choice 0 to its simplest value, so the shrinker only has to make the tape shorter and its numbers smaller.
struct TapeSpan {
    size_t begin = 0;
    size_t end = 0;
    bool listElement = false;  // [continue flag, element choices]: deleting it removes one list element
};

struct Overrun {};   // the case drew more than maxDraws choices
struct Discard {     // filter() could not be satisfied or a precondition rejected the case
    std::string reason;
};

class Tape {
  public:
    Tape() = default;
    static Tape random(uint64_t seed, double size);
    static Tape replay(std::vector<uint64_t> choices, double size = 1.0);

    // Uniform in [0, bound] (inclusive) when random, else the recorded choice clamped to bound.
    uint64_t draw(uint64_t bound);
    // Like draw, but a fresh (random) tape records `value` instead of a random number. Lets a generator bias its distribution
    // (edge values, small offsets) while the recorded choice stays shrinkable over the full range.
    uint64_t drawForced(uint64_t bound, uint64_t value);
    [[nodiscard]] const std::vector<uint64_t>& drawn() const { return m_drawn; }
    [[nodiscard]] const std::vector<TapeSpan>& spans() const { return m_spans; }
    [[nodiscard]] size_t position() const { return m_drawn.size(); }
    size_t beginSpan() { return m_drawn.size(); }
    void endSpan(size_t begin, bool listElement = false) {
        if (m_drawn.size() > begin) m_spans.push_back({begin, m_drawn.size(), listElement});
    }
    [[nodiscard]] double size() const { return m_size; }
    size_t maxDraws = 200000;

  private:
    bool m_random = true;
    uint64_t m_state = 0;
    double m_size = 1.0;
    std::vector<uint64_t> m_prefix;
    std::vector<uint64_t> m_drawn;
    std::vector<TapeSpan> m_spans;
    uint64_t next();
};

// ---------------------------------------------------------------- generators

enum class GenKind : uint8_t { Int, Float, Bool, Just, Elements, OneOf, Frequency, ListOf, UnitVec, Rotation, Nasty, Text, Ident, Map, Filter, Bind, Lift, ListLit, RecordLit, Delay, Subseq };

struct GenV {
    GenKind kind = GenKind::Just;
    int64_t lo = 0;
    int64_t hi = 0;
    double flo = 0.0;
    double fhi = 0.0;
    ValueList values;        // alternatives, list literal items, lifted positional args
    std::vector<uint64_t> weights;
    NamedArgs named;         // record literal fields, lifted named args
    Value fn;                // map/filter/bind/delay function
    std::string label;       // lifted builtin name (diagnostics)
    NativeFn lift;           // lifted builtin
    SrcLoc loc;
};

[[nodiscard]] Value makeGen(std::shared_ptr<GenV> gen);

// ---------------------------------------------------------------- host interface

// A live world driven by a scene program. Created fresh for every after/always/eventually block and dropped at its end.
class Sim {
  public:
    virtual ~Sim() = default;
    // Interprets one action. Must call onTick() after every simulated tick; when onTick returns false the action stops early.
    virtual void apply(const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick) = 0;
    virtual void tick() = 0;
    virtual Value observe(const std::string& name, Interp& interp, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) = 0;
    [[nodiscard]] virtual uint64_t ticks() const = 0;
};

struct ActionSpec {
    std::string name;
    std::vector<std::string> params;
    std::string doc;
};

struct ObservationSpec {
    std::string name;
    std::string signature;
    std::string doc;
};

struct Program;

class Host {
  public:
    virtual ~Host() = default;
    [[nodiscard]] virtual std::unique_ptr<Sim> createSim() = 0;
    [[nodiscard]] virtual const std::vector<ActionSpec>& actions() const = 0;
    [[nodiscard]] virtual const std::vector<ObservationSpec>& observations() const = 0;
    // Extra natives that do not need a world (for example JSON round trips through the engine's parser).
    virtual void addBuiltins(Program& program) { (void)program; }
};

// ---------------------------------------------------------------- program

struct Program {
    std::map<std::string, Value> globals;
    std::vector<std::shared_ptr<LawDecl>> laws;
    std::vector<std::shared_ptr<ExampleDecl>> examples;
    std::vector<std::string> files;
    Host* host = nullptr;

    [[nodiscard]] const LawDecl* law(const std::string& name) const;
    void define(const std::string& name, Value value, const SrcLoc& loc);
    void native(const std::string& name, NativeFn fn, bool liftable = false);
};

// Loads a file and its includes (paths relative to the including file). Throws BentError.
[[nodiscard]] std::unique_ptr<Program> loadProgram(const std::string& path, Host* host);
[[nodiscard]] std::unique_ptr<Program> loadProgramSource(const std::string& source, const std::string& name, Host* host);
void installBuiltins(Program& program);

// ---------------------------------------------------------------- interpreter

struct Interp {
    explicit Interp(const Program& program) : prog(program) {}

    const Program& prog;
    Tape* tape = nullptr;
    std::vector<Sim*> sims;
    bool explain = false;
    std::vector<std::string> notes;
    int depth = 0;
    int quiet = 0;  // > 0 while evaluating premises (==> left, or left, not, if conditions, exists bodies): their false parts are not notes
    uint64_t steps = 0;
    uint64_t maxSteps = 200000000;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();

    Value eval(const ExprPtr& expr, const EnvPtr& env);
    Value call(const Value& fn, const ValueList& args, const NamedArgs& named, const SrcLoc& loc);
    // Draws a value from a generator (or returns a plain value; lists and records containing generators are drawn element-wise).
    Value realize(const Value& value, const SrcLoc& loc);
    Value draw(const GenV& gen);
    [[nodiscard]] Sim& currentSim(const SrcLoc& loc, const std::string& what);
    void note(std::string text);
    // Runs a scene program on a fresh world. kind/ticks as in the temporal expression; check is evaluated as the body.
    Value runTemporal(TemporalKind kind, const Value& program, int64_t extraTicks, const std::function<Value()>& body, const SrcLoc& loc);
};

[[nodiscard]] std::vector<const ActionV*> programActions(const Value& program, const SrcLoc& loc);

// Helpers for natives.
[[nodiscard]] double argNumber(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] int64_t argInt(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] const std::string& argString(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] const ValueList& argList(const Value& value, const SrcLoc& loc, const char* what);
[[nodiscard]] std::string truncate(const std::string& text, size_t limit = 300);

}
