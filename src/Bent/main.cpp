#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "Engine/Bent/Check.hpp"
#include "Engine/Bent/EngineHost.hpp"
#include "Engine/Bent/Ext_Gpu.hpp"

namespace {

const char* kUsage = R"(usage:
  bent check FILE.bent [--cases N] [--seed S] [--time SEC] [--jobs J] [--law NAME]... [--no-shrink] [--coverage]
                       [--shrink-limit N] [--isolate] [--case-timeout SEC] [--regressions DIR | --no-regressions]
                       [--json] [--json-out FILE] [--verbose] [--gpu]
  bent replay FILE.bent --seed S --case K [--law NAME] [--isolate]
  bent shrink FILE.bent --seed S --case K [--law NAME] [--regressions DIR] [--isolate]
  bent list FILE.bent | bent list --actions | bent list --observations | bent list --builtins
  bent suite --laws DIR [--regressions DIR] [--cases N] [--seed S] [--jobs J] [--isolate] [--gpu]
  bent memsafe --laws DIR [--cases N] [--seed S] [--jobs J]   (needs a LIT_SANITIZE build; exit 77 otherwise)
  bent eval [FILE.bent] --expr "EXPRESSION" [--seed S]

check runs every law declared in FILE (all loaded laws with --law) for N cases (default 100) in parallel,
shrinks the first failure to a minimal counterexample, prints it as a runnable .bent program and writes it to
the regressions directory. Examples (regression files) loaded by FILE are run too.
--gpu enables GPU mode: laws under a gpu/ directory render real frames through LitShot (tests/bent/laws/gpu, docs/Bent.md). Without it
such laws are skipped; with it and no usable GPU or LitShot the exit status is 77 (ctest skip).
Exit status: 0 all laws hold, 1 a law failed, 2 usage or language error.
)";

struct Cli {
    std::string command;
    std::vector<std::string> files;
    bent::CheckOptions options;
    bool casesGiven = false;
    bool json = false;
    std::string jsonOut;
    bool verbose = false;
    bool noRegressions = false;
    bool regressionsGiven = false;
    int64_t caseIndex = -1;
    std::string lawsDir;
    std::string expr;
    std::string listWhat;
    bool memsafe = false;
    std::vector<std::string> argvCopy;
};

[[noreturn]] void usage(const std::string& message) {
    if (!message.empty()) std::fprintf(stderr, "bent: %s\n", message.c_str());
    std::fputs(kUsage, stderr);
    std::exit(2);
}

Cli parse(int argc, char** argv) {
    Cli cli;
    if (argc < 2) usage("");
    cli.command = argv[1];
    if (cli.command == "-h" || cli.command == "--help" || cli.command == "help") {
        std::fputs(kUsage, stdout);
        std::exit(0);
    }
    std::string commandLine = "bent";
    for (int i = 0; i < argc; ++i) cli.argvCopy.push_back(argv[i]);
    for (int i = 1; i < argc; ++i) commandLine += std::string(" ") + argv[i];
    cli.options.commandLine = commandLine;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(arg + " needs a value");
            return argv[++i];
        };
        const auto number = [&]() -> double {
            const std::string text = value();
            char* end = nullptr;
            const double v = std::strtod(text.c_str(), &end);
            if (!end || *end) usage(arg + ": not a number: " + text);
            return v;
        };
        if (arg == "--cases") {
            cli.options.cases = static_cast<int64_t>(number());
            cli.casesGiven = true;
        } else if (arg == "--seed") {
            cli.options.seed = std::strtoull(value().c_str(), nullptr, 10);
        } else if (arg == "--time") {
            cli.options.timeBudget = number();
        } else if (arg == "--jobs" || arg == "-j") {
            cli.options.jobs = static_cast<int>(number());
        } else if (arg == "--law") {
            cli.options.lawFilter.push_back(value());
        } else if (arg == "--no-shrink") {
            cli.options.shrink = false;
        } else if (arg == "--coverage") {
            cli.options.coverage = true;
        } else if (arg == "--shrink-limit") {
            cli.options.shrinkLimit = static_cast<int64_t>(number());
        } else if (arg == "--shrink-time") {
            cli.options.shrinkSeconds = number();
        } else if (arg == "--gpu") {
            bent::setGpuMode(true);
        } else if (arg == "--isolate") {
            cli.options.isolate = true;
        } else if (arg == "--case-timeout") {
            cli.options.caseTimeout = number();
        } else if (arg == "--regressions") {
            cli.options.regressionDir = value();
            cli.regressionsGiven = true;
        } else if (arg == "--no-regressions") {
            cli.noRegressions = true;
        } else if (arg == "--json") {
            cli.json = true;
        } else if (arg == "--json-out") {
            cli.jsonOut = value();
        } else if (arg == "--verbose" || arg == "-v") {
            cli.verbose = true;
        } else if (arg == "--case") {
            cli.caseIndex = static_cast<int64_t>(number());
        } else if (arg == "--laws") {
            cli.lawsDir = value();
        } else if (arg == "--expr" || arg == "-e") {
            cli.expr = value();
        } else if (arg == "--actions" || arg == "--observations" || arg == "--builtins") {
            cli.listWhat = arg.substr(2);
        } else if (!arg.empty() && arg[0] == '-') {
            usage("unknown option " + arg);
        } else {
            cli.files.push_back(arg);
        }
    }
    return cli;
}

FILE* human(const Cli& cli) { return cli.json ? stderr : stdout; }

std::string defaultRegressionDir(const std::string& file) {
    std::error_code ec;
    if (std::filesystem::is_directory("tests/bent/regressions", ec)) return "tests/bent/regressions";
    const std::filesystem::path dir = std::filesystem::path(file).parent_path();
    if (std::filesystem::is_directory(dir.parent_path() / "regressions", ec)) return (dir.parent_path() / "regressions").string();
    return (dir / "regressions").string();
}

void emitJson(const Cli& cli, const std::vector<bent::LawReport>& laws, const std::vector<bent::ExampleReport>& examples) {
    if (!cli.json && cli.jsonOut.empty()) return;
    const std::string text = bent::reportsJson(laws, examples, cli.options);
    if (cli.json) std::fwrite(text.data(), 1, text.size(), stdout);
    if (!cli.jsonOut.empty()) {
        std::ofstream out(cli.jsonOut);
        out << text;
    }
}

int printExamples(const Cli& cli, const std::vector<bent::ExampleReport>& examples) {
    int failures = 0;
    for (const bent::ExampleReport& e : examples) {
        const char* label = e.verdict == "pass" ? "PASS" : (e.verdict == "known-fail" ? "KNOWN" : (e.verdict == "fixed" ? "FIXED?" : "FAIL"));
        std::fprintf(human(cli), "%-8s example %s (%s:%d)%s%s\n", label, e.law.c_str(), e.file.c_str(), e.line, e.known ? " known: " : "", e.reason.c_str());
        if (e.verdict == "fail") {
            ++failures;
            std::fprintf(human(cli), "  %s: %s\n", bent::caseStatusName(e.outcome.status), e.outcome.message.c_str());
            for (const std::string& note : e.outcome.notes) std::fprintf(human(cli), "    | %s\n", note.c_str());
        }
        if (e.verdict == "fixed") std::fprintf(human(cli), "  known failure now holds: remove `known` from the example if the bug is fixed\n");
    }
    return failures;
}

const bent::LawDecl* pickLaw(const Cli& cli, const bent::Program& program) {
    if (!cli.options.lawFilter.empty()) {
        const bent::LawDecl* law = program.law(cli.options.lawFilter.front());
        if (!law) usage("no law named " + cli.options.lawFilter.front());
        return law;
    }
    std::vector<const bent::LawDecl*> own = bent::selectLaws(program, {}, cli.files.front());
    if (own.size() != 1) usage("the file declares " + std::to_string(own.size()) + " laws: pick one with --law NAME");
    return own.front();
}

bool gpuFile(const std::string& path) { return path.find("/gpu/") != std::string::npos || path.rfind("gpu/", 0) == 0; }

int gpuSkip() {
    if (!bent::gpuMode()) {
        std::fprintf(stderr, "SKIP: GPU laws only run with --gpu\n");
        return 0;
    }
    if (!bent::gpuUsable()) {
        std::fprintf(stderr, "SKIP: no usable GPU or LitShot (build the LitShot target)\n");
        return 77;
    }
    return -1;
}

int runCheck(Cli& cli, bent::Host& host) {
    if (cli.files.size() != 1) usage("check needs exactly one .bent file");
    if (!cli.noRegressions && !cli.regressionsGiven) cli.options.regressionDir = defaultRegressionDir(cli.files.front());
    if (cli.noRegressions) cli.options.regressionDir.clear();
    if (cli.options.timeBudget > 0.0 && !cli.casesGiven) cli.options.cases = 1000000000;
    if (gpuFile(cli.files.front())) {
        if (const int skip = gpuSkip(); skip >= 0) return skip;
    }
    const std::unique_ptr<bent::Program> program = bent::loadProgram(cli.files.front(), &host);
    const std::vector<const bent::LawDecl*> laws = bent::selectLaws(*program, cli.options.lawFilter, cli.files.front());
    if (!cli.options.lawFilter.empty() && laws.size() != cli.options.lawFilter.size()) usage("some --law names are not declared in the loaded files");
    cli.options.progress = [&](const std::string& text) { std::fprintf(stderr, "%s\n", text.c_str()); };
    std::vector<bent::LawReport> reports;
    int failures = 0;
    const double lawBudget = cli.options.timeBudget > 0.0 && !laws.empty() ? cli.options.timeBudget / static_cast<double>(laws.size()) : 0.0;
    for (const bent::LawDecl* law : laws) {
        reports.push_back(bent::checkLaw(*program, *law, cli.options, lawBudget));
        std::fputs(bent::formatReport(reports.back()).c_str(), human(cli));
        std::fflush(human(cli));
        if (reports.back().status == "vacuous") ++failures;
        if (reports.back().status == "fail" || reports.back().status == "error") {
            ++failures;
            std::fprintf(human(cli), "  counterexample program:\n");
            std::fputs(reports.back().counterexample.c_str(), human(cli));
        }
    }
    std::vector<bent::ExampleReport> examples;
    if (cli.options.lawFilter.empty()) examples = bent::runExamples(*program, cli.options);
    failures += printExamples(cli, examples);
    int64_t total = 0;
    for (const auto& r : reports) total += r.cases;
    std::fprintf(human(cli), "%zu laws, %lld cases, %zu examples, %d failing\n", reports.size(), static_cast<long long>(total), examples.size(), failures);
    emitJson(cli, reports, examples);
    return failures ? 1 : 0;
}

int runReplay(Cli& cli, bent::Host& host) {
    if (cli.files.size() != 1 || cli.caseIndex < 0) usage("replay needs FILE --seed S --case K");
    const std::unique_ptr<bent::Program> program = bent::loadProgram(cli.files.front(), &host);
    const bent::LawDecl* law = pickLaw(cli, *program);
    bent::CaseOutcome outcome;
    bent::Tape tape = bent::Tape::random(bent::caseSeed(cli.options.seed, law->name, cli.caseIndex), bent::caseSize(cli.caseIndex));
    outcome = bent::runCase(*program, *law, tape, true, cli.options.caseTimeout);
    std::fprintf(human(cli), "law %s, seed %llu, case %lld: %s %s\n", law->name.c_str(), static_cast<unsigned long long>(cli.options.seed), static_cast<long long>(cli.caseIndex), bent::caseStatusName(outcome.status), outcome.message.c_str());
    for (const auto& [name, value] : outcome.bindings) std::fprintf(human(cli), "  %s = %s\n", name.c_str(), bent::prettyValue(value, 2).c_str());
    for (const std::string& note : outcome.notes) std::fprintf(human(cli), "  | %s\n", note.c_str());
    std::fprintf(human(cli), "  %zu choices, %.2f ms\n", outcome.tape.size(), outcome.millis);
    if (cli.json) {
        bent::LawReport report;
        report.law = law->name;
        report.file = law->file;
        report.status = bent::caseStatusName(outcome.status);
        report.cases = 1;
        report.failingCase = outcome.status == bent::CaseStatus::Pass ? -1 : cli.caseIndex;
        report.original = outcome;
        report.shrunk = outcome;
        emitJson(cli, {report}, {});
    }
    return outcome.status == bent::CaseStatus::Pass || outcome.status == bent::CaseStatus::Discard ? 0 : 1;
}

int runShrink(Cli& cli, bent::Host& host) {
    if (cli.files.size() != 1 || cli.caseIndex < 0) usage("shrink needs FILE --seed S --case K");
    if (!cli.noRegressions && !cli.regressionsGiven) cli.options.regressionDir = defaultRegressionDir(cli.files.front());
    if (cli.noRegressions) cli.options.regressionDir.clear();
    const std::unique_ptr<bent::Program> program = bent::loadProgram(cli.files.front(), &host);
    const bent::LawDecl* law = pickLaw(cli, *program);
    const bent::LawReport report = bent::shrinkCase(*program, *law, cli.options, cli.caseIndex);
    std::fputs(bent::formatReport(report).c_str(), human(cli));
    if (!report.counterexample.empty()) std::fputs(report.counterexample.c_str(), human(cli));
    emitJson(cli, {report}, {});
    return report.status == "pass" ? 0 : 1;
}

int runList(Cli& cli, bent::Host& host) {
    if (cli.listWhat == "actions") {
        for (const bent::ActionSpec& spec : host.actions()) {
            std::string params;
            for (const std::string& p : spec.params) params += (params.empty() ? "" : ", ") + p;
            std::printf("%s(%s)\n    %s\n", spec.name.c_str(), params.c_str(), spec.doc.c_str());
        }
        return 0;
    }
    if (cli.listWhat == "observations") {
        for (const bent::ObservationSpec& spec : host.observations()) std::printf("%s\n    %s\n", spec.signature.c_str(), spec.doc.c_str());
        return 0;
    }
    if (cli.listWhat == "builtins") {
        const std::unique_ptr<bent::Program> program = bent::loadProgramSource("", "<builtins>", &host);
        for (const auto& [name, value] : program->globals) std::printf("%s\n", name.c_str());
        return 0;
    }
    if (cli.files.size() != 1) usage("list needs a .bent file (or --actions / --observations / --builtins)");
    const std::unique_ptr<bent::Program> program = bent::loadProgram(cli.files.front(), &host);
    for (const auto& law : program->laws) {
        std::string vars;
        for (const auto& [name, gen] : law->vars) vars += (vars.empty() ? "" : ", ") + name;
        std::printf("law %-40s %s:%d  forall (%s)%s\n", law->name.c_str(), law->file.c_str(), law->loc.line, vars.c_str(), law->where ? " where ..." : "");
    }
    for (const auto& example : program->examples) std::printf("example %-36s %s:%d%s\n", example->law.c_str(), example->file.c_str(), example->loc.line, example->known ? "  (known failure)" : "");
    return 0;
}

int runSuite(Cli& cli, bent::Host& host);

int runMemsafe(Cli& cli, bent::Host& host) {
#if defined(__SANITIZE_ADDRESS__)
    if (!cli.casesGiven) cli.options.cases = 6;
    cli.options.isolate = true;
    cli.memsafe = true;
    const std::string prefix = "/tmp/bent-memsafe-" + std::to_string(::getpid());
    if (!std::getenv("BENT_MEMSAFE_CHILD")) {
        ::setenv("BENT_MEMSAFE_CHILD", "1", 1);
        ::setenv("ASAN_OPTIONS", ("detect_stack_use_after_return=1:strict_string_checks=1:detect_leaks=0:abort_on_error=0:log_path=" + prefix).c_str(), 1);
        ::setenv("UBSAN_OPTIONS", ("halt_on_error=1:print_stacktrace=1:log_path=" + prefix).c_str(), 1);
        std::vector<std::string> keep(cli.argvCopy);
        std::vector<char*> args;
        for (std::string& a : keep) args.push_back(a.data());
        args.push_back(nullptr);
        ::execv("/proc/self/exe", args.data());
        std::fprintf(stderr, "bent: re-exec failed\n");
        return 2;
    }
    bent::setSanitizerLogPrefix(prefix);
    return runSuite(cli, host);
#else
    (void)cli; (void)host;
    std::fputs("bent memsafe: this binary is not sanitizer-instrumented (configure with -DLIT_SANITIZE=address,undefined); skipping\n", stderr);
    return 77;
#endif
}

int runSuite(Cli& cli, bent::Host& host) {
    if (cli.lawsDir.empty()) usage("suite needs --laws DIR");
    if (gpuFile(cli.lawsDir + "/")) {
        if (const int skip = gpuSkip(); skip >= 0) return skip;
    }
    if (!cli.casesGiven) cli.options.cases = 30;
    cli.options.shrinkLimit = std::min<int64_t>(cli.options.shrinkLimit, 600);
    cli.options.shrinkSeconds = std::min(cli.options.shrinkSeconds, 30.0);
    const std::string regressions = cli.options.regressionDir;
    cli.options.regressionDir.clear();
    const auto bentFiles = [](const std::string& dir) {
        std::vector<std::filesystem::path> files;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() == ".bent") files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());
        return files;
    };
    std::vector<bent::ExampleReport> examples;
    int failures = 0;
    int memsafeHits = 0;
    std::vector<std::string> knownLaws;
    if (!regressions.empty()) {
        for (const auto& file : bentFiles(regressions)) {
            const std::unique_ptr<bent::Program> program = bent::loadProgram(file.string(), &host);
            std::vector<bent::ExampleReport> found = bent::runExamples(*program, cli.options);
            failures += printExamples(cli, found);
            for (const auto& e : found) {
                if (e.verdict == "known-fail") knownLaws.push_back(e.law);
            }
            examples.insert(examples.end(), found.begin(), found.end());
        }
    }
    std::vector<bent::LawReport> reports;
    int64_t total = 0;
    int expected = 0;
    for (const auto& file : bentFiles(cli.lawsDir)) {
        const std::unique_ptr<bent::Program> program = bent::loadProgram(file.string(), &host);
        for (const bent::LawDecl* law : bent::selectLaws(*program, {}, file.string())) {
            reports.push_back(bent::checkLaw(*program, *law, cli.options, 0.0));
            total += reports.back().cases;
            const bool failed = reports.back().status == "fail" || reports.back().status == "error" || reports.back().status == "vacuous";
            const bool known = std::find(knownLaws.begin(), knownLaws.end(), law->name) != knownLaws.end();
            if (failed && known) {
                ++expected;
                std::fprintf(human(cli), "KNOWN    %-40s fails as recorded by its known regression (case %lld: %s)\n", law->name.c_str(), static_cast<long long>(reports.back().failingCase), bent::truncate(reports.back().original.message, 120).c_str());
            } else {
                std::fputs(bent::formatReport(reports.back()).c_str(), human(cli));
                if (cli.memsafe) {
                    const std::string& m = reports.back().original.message;
                    const bool hit = failed && (m.find("memory-safety report") != std::string::npos || m.find("engine crashed") != std::string::npos || m.find("child exited") != std::string::npos);
                    if (hit) {
                        ++memsafeHits;
                        std::fprintf(human(cli), "MEMSAFE  law %s case %lld seed %llu: replay with: bent replay %s --law %s --seed %llu --case %lld --isolate\n", law->name.c_str(), static_cast<long long>(reports.back().failingCase), static_cast<unsigned long long>(cli.options.seed), file.string().c_str(), law->name.c_str(), static_cast<unsigned long long>(cli.options.seed), static_cast<long long>(reports.back().failingCase));
                    }
                } else if (failed) {
                    ++failures;
                }
            }
            std::fflush(human(cli));
        }
    }
    std::fprintf(human(cli), "suite: %zu laws, %lld cases, %zu regressions (%zu known failures, %d laws failing as known), %d unexpected failures\n", reports.size(), static_cast<long long>(total), examples.size(), knownLaws.size(), expected, failures);
    emitJson(cli, reports, examples);
    if (cli.memsafe) {
        std::fprintf(human(cli), "memory_safe: %s (%d sanitizer/crash hits over %lld isolated cases; law violations that are not memory errors are not counted here)\n", memsafeHits ? "FAIL" : "ok", memsafeHits, static_cast<long long>(total));
        return memsafeHits ? 1 : 0;
    }
    return failures ? 1 : 0;
}

int runEval(Cli& cli, bent::Host& host) {
    if (cli.expr.empty()) usage("eval needs --expr");
    const std::unique_ptr<bent::Program> program = cli.files.empty() ? bent::loadProgramSource("", "<eval>", &host) : bent::loadProgram(cli.files.front(), &host);
    bent::Interp in(*program);
    bent::Tape tape = bent::Tape::random(cli.options.seed, 1.0);
    in.tape = &tape;
    in.explain = true;
    const bent::Value value = in.realize(in.eval(bent::parseExpression(bent::lex(cli.expr, "<expr>")), nullptr), bent::SrcLoc{"<expr>", 1, 1});
    std::printf("%s\n", bent::prettyValue(value, 0).c_str());
    for (const std::string& note : in.notes) std::printf("  | %s\n", note.c_str());
    return 0;
}

}

int main(int argc, char** argv) {
    Cli cli = parse(argc, argv);
    bent::EngineHost::silenceEngineLog(!cli.verbose);
    bent::installCrashReporter();
    bent::EngineHost host;
    try {
        if (cli.command == "check") return runCheck(cli, host);
        if (cli.command == "replay") return runReplay(cli, host);
        if (cli.command == "shrink") return runShrink(cli, host);
        if (cli.command == "list") return runList(cli, host);
        if (cli.command == "suite") return runSuite(cli, host);
        if (cli.command == "memsafe") return runMemsafe(cli, host);
        if (cli.command == "eval") return runEval(cli, host);
        usage("unknown command " + cli.command);
    } catch (const bent::BentError& error) {
        std::fprintf(stderr, "bent: %s\n", error.what());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "bent: %s\n", error.what());
        return 2;
    }
}
