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
    std::vector<uint64_t> tape;
    std::vector<TapeSpan> spans;
    NamedArgs bindings;
    std::vector<std::string> notes;
    std::vector<std::pair<std::string, bool>> covers;
    double millis = 0.0;
};

[[nodiscard]] uint64_t caseSeed(uint64_t seed, const std::string& law, int64_t caseIndex);
[[nodiscard]] double caseSize(int64_t caseIndex);

[[nodiscard]] CaseOutcome runCase(const Program& program, const LawDecl& law, Tape& tape, bool explain, double timeLimitSeconds);
[[nodiscard]] CaseOutcome runExample(const Program& program, const ExampleDecl& example, bool explain, double timeLimitSeconds);

struct CheckOptions {
    int64_t cases = 100;
    uint64_t seed = 1;
    double timeBudget = 0.0;
    int jobs = 0;
    std::vector<std::string> lawFilter;
    bool shrink = true;
    int64_t shrinkLimit = 4000;
    double shrinkSeconds = 180.0;
    bool isolate = false;
    bool coverage = false;
    double caseTimeout = 120.0;
    std::string regressionDir;
    std::function<void(const std::string&)> progress;
};

struct LawReport {
    std::string law;
    std::string file;
    std::string status;
    int64_t cases = 0;
    int64_t passed = 0;
    int64_t discards = 0;
    int64_t failingCase = -1;
    uint64_t seed = 0;
    CaseOutcome original;
    CaseOutcome shrunk;
    int64_t shrinkAttempts = 0;
    int64_t shrinkAccepted = 0;
    std::string counterexample;
    std::string regressionPath;
    double seconds = 0.0;
    std::vector<std::pair<std::string, std::pair<int64_t, int64_t>>> cover;
    bool showCover = false;
};

struct ExampleReport {
    std::string law;
    std::string file;
    int line = 0;
    bool known = false;
    std::string reason;
    CaseOutcome outcome;
    std::string verdict;
};

[[nodiscard]] std::vector<const LawDecl*> selectLaws(const Program& program, const std::vector<std::string>& filter, const std::string& topFile);
[[nodiscard]] LawReport checkLaw(const Program& program, const LawDecl& law, const CheckOptions& options, double lawBudgetSeconds);
[[nodiscard]] LawReport shrinkCase(const Program& program, const LawDecl& law, const CheckOptions& options, int64_t caseIndex);
[[nodiscard]] std::vector<ExampleReport> runExamples(const Program& program, const CheckOptions& options);

[[nodiscard]] std::string counterexampleProgram(const LawDecl& law, const NamedArgs& bindings, const std::string& includePath);
[[nodiscard]] std::string prettyValue(const Value& value, int indent);
[[nodiscard]] std::string formatReport(const LawReport& report);
[[nodiscard]] std::string reportsJson(const std::vector<LawReport>& laws, const std::vector<ExampleReport>& examples, const CheckOptions& options);
[[nodiscard]] std::string jsonEscape(const std::string& text);

void installCrashReporter();
void setSanitizerLogPrefix(const std::string& prefix);

}
