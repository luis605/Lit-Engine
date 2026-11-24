#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent {

enum class CaseStatus : uint8_t { Pass, Fail, Discard, Error };
[[nodiscard]] const char* caseStatusName(CaseStatus status);

struct CaseOutcome {
    CaseStatus status = CaseStatus::Pass;
    std::string message;
    std::vector<uint64_t> tape;  // choices actually consumed
    std::vector<TapeSpan> spans;
    NamedArgs bindings;
    std::vector<std::string> notes;
    double millis = 0.0;
};

// Deterministic per-case seed and size: case k of law L under seed S is always the same tape.
[[nodiscard]] uint64_t caseSeed(uint64_t seed, const std::string& law, int64_t caseIndex);
[[nodiscard]] double caseSize(int64_t caseIndex);

// Runs one case of a law from a tape (random or replayed).
[[nodiscard]] CaseOutcome runCase(const Program& program, const LawDecl& law, Tape& tape, bool explain, double timeLimitSeconds);
// Runs a law on explicit bindings (examples / regressions).
[[nodiscard]] CaseOutcome runExample(const Program& program, const ExampleDecl& example, bool explain, double timeLimitSeconds);

struct CheckOptions {
    int64_t cases = 100;
    uint64_t seed = 1;
    double timeBudget = 0.0;  // seconds for all selected laws together, 0: unlimited
    int jobs = 0;             // 0: hardware threads
    std::vector<std::string> lawFilter;
    bool shrink = true;
    int64_t shrinkLimit = 4000;    // attempts
    double shrinkSeconds = 180.0;  // per failing law
    bool isolate = false;          // fork a process per case (crash and hang safe)
    double caseTimeout = 120.0;    // seconds per case
    std::string regressionDir;     // write <law>-<hash>.bent here when non-empty
    std::string commandLine;       // recorded in regression headers
    std::function<void(const std::string&)> progress;
};

struct LawReport {
    std::string law;
    std::string file;
    std::string status;  // pass, fail, error, gave-up
    int64_t cases = 0;
    int64_t passed = 0;
    int64_t discards = 0;
    int64_t failingCase = -1;
    uint64_t seed = 0;
    CaseOutcome original;
    CaseOutcome shrunk;
    int64_t shrinkAttempts = 0;
    int64_t shrinkAccepted = 0;
    std::string counterexample;  // runnable .bent text
    std::string regressionPath;
    double seconds = 0.0;
};

struct ExampleReport {
    std::string law;
    std::string file;
    int line = 0;
    bool known = false;
    std::string reason;
    CaseOutcome outcome;
    // pass: holds; known-fail: known and still failing (expected); fixed: known but passes now; fail: unexpected failure.
    std::string verdict;
};

[[nodiscard]] std::vector<const LawDecl*> selectLaws(const Program& program, const std::vector<std::string>& filter, const std::string& topFile);
[[nodiscard]] LawReport checkLaw(const Program& program, const LawDecl& law, const CheckOptions& options, double lawBudgetSeconds);
[[nodiscard]] LawReport shrinkCase(const Program& program, const LawDecl& law, const CheckOptions& options, int64_t caseIndex);
[[nodiscard]] std::vector<ExampleReport> runExamples(const Program& program, const CheckOptions& options);

[[nodiscard]] std::string counterexampleProgram(const LawDecl& law, const NamedArgs& bindings, const std::string& includePath, const std::vector<std::string>& header);
[[nodiscard]] std::string prettyValue(const Value& value, int indent);
[[nodiscard]] std::string formatReport(const LawReport& report);
[[nodiscard]] std::string reportsJson(const std::vector<LawReport>& laws, const std::vector<ExampleReport>& examples, const CheckOptions& options);
[[nodiscard]] std::string jsonEscape(const std::string& text);

// Installs SIGSEGV/SIGABRT/SIGFPE handlers that name the law and case being run on the crashing thread.
void installCrashReporter();

}
