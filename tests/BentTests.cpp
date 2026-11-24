// Unit tests of the Bent language core: lexer, parser, evaluator, generators, shrinker, checker (no engine).
#include "TestSupport.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Engine/Bent/Check.hpp"

using namespace bent;

namespace {

// A toy world: a counter that actions add to and ticks advance.
class CounterSim final : public Sim {
  public:
    void apply(const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick) override {
        if (action.name == "add") {
            const Value* n = action.arg("n");
            value += n ? argInt(*n, loc, "n") : 1;
        } else if (action.name == "wait") {
            const Value* n = action.arg("n");
            const int64_t count = n ? argInt(*n, loc, "n") : 1;
            for (int64_t i = 0; i < count; ++i) {
                tick();
                if (!onTick()) return;
            }
        } else if (action.name == "crash") {
            std::abort();
        } else {
            throw BentError(loc, "unknown action " + action.name);
        }
    }
    void tick() override {
        ++t;
        value += drift;
    }
    Value observe(const std::string& name, Interp&, const ValueList&, const NamedArgs&, const SrcLoc& loc) override {
        if (name == "value") return Value::integer(value);
        if (name == "now") return Value::integer(t);
        throw BentError(loc, "unknown observation " + name);
    }
    [[nodiscard]] uint64_t ticks() const override { return static_cast<uint64_t>(t); }
    int64_t value = 0;
    int64_t t = 0;
    int64_t drift = 0;
};

class CounterHost final : public Host {
  public:
    std::unique_ptr<Sim> createSim() override { return std::make_unique<CounterSim>(); }
    const std::vector<ActionSpec>& actions() const override { return m_actions; }
    const std::vector<ObservationSpec>& observations() const override { return m_observations; }
    void addBuiltins(Program& program) override {
        for (const ActionSpec& spec : m_actions) {
            program.native(spec.name, [spec](Interp&, const ValueList& args, const NamedArgs& named, const SrcLoc&) {
                NamedArgs fields = named;
                for (size_t i = 0; i < args.size() && i < spec.params.size(); ++i) fields.emplace_back(spec.params[i], args[i]);
                return Value::action(spec.name, fields);
            }, true);
        }
        for (const ObservationSpec& spec : m_observations) {
            const std::string name = spec.name;
            program.native(name, [name](Interp& in, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) { return in.currentSim(loc, name).observe(name, in, args, named, loc); });
        }
    }

  private:
    std::vector<ActionSpec> m_actions = {{"add", {"n"}, ""}, {"wait", {"n"}, ""}, {"crash", {}, ""}};
    std::vector<ObservationSpec> m_observations = {{"value", "value()", ""}, {"now", "now()", ""}};
};

CounterHost gHost;

std::unique_ptr<Program> load(const std::string& source) { return loadProgramSource(source, "test.bent", &gHost); }

Value evalIn(const Program& program, const std::string& text, uint64_t seed = 1) {
    Interp in(program);
    Tape tape = Tape::random(seed, 1.0);
    in.tape = &tape;
    return in.realize(in.eval(parseExpression(lex(text, "expr")), nullptr), SrcLoc{});
}

Value evalText(const std::string& text) {
    static const std::unique_ptr<Program> program = load("");
    return evalIn(*program, text);
}

std::string errorOf(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const BentError& error) {
        return error.what();
    }
    return {};
}

void testLexer() {
    const std::vector<Token> tokens = lex("let x = 1.5e3 ++ \"a\\n\\u{41}\" # c\n==> ~= 0x10", "f.bent");
    CHECK(tokens.size() == 10);
    CHECK(tokens[3].kind == TokKind::Float && tokens[3].fval == 1500.0);
    CHECK(tokens[5].kind == TokKind::String && tokens[5].text == "a\nA");
    CHECK(tokens[6].text == "==>" && tokens[6].loc.line == 2 && tokens[6].loc.col == 1);
    CHECK(tokens[8].kind == TokKind::Int && tokens[8].ival == 16);
    CHECK(errorOf([] { (void)lex("a\n  $", "f.bent"); }).find("f.bent:2:3") != std::string::npos);
    CHECK(errorOf([] { (void)lex("\"abc", "f.bent"); }).find("unterminated string") != std::string::npos);
    CHECK(errorOf([] { (void)lex("12abc", "f.bent"); }).find("1:3") != std::string::npos);
}

void testParser() {
    CHECK(equal(evalText("1 + 2 * 3"), Value::integer(7)));
    CHECK(equal(evalText("(1 + 2) * 3"), Value::integer(9)));
    CHECK(equal(evalText("1 < 2 and 2 < 3 or false"), Value::boolean(true)));
    CHECK(equal(evalText("not 1 == 2"), Value::boolean(true)));
    CHECK(equal(evalText("false ==> 1 / 0 == 0"), Value::boolean(true)));
    CHECK(equal(evalText("[1, 2] ++ [3]"), evalText("[1, 2, 3]")));
    CHECK(equal(evalText("{b: 1, a: 2}.a"), Value::integer(2)));
    CHECK(equal(evalText("vec3(1, 2, 3).y"), Value::number(2.0)));
    CHECK(equal(evalText("[10, 20, 30][-1]"), Value::integer(30)));
    const std::string err = errorOf([] { (void)load("law broken {\n  forall (x int(1, 2)) => true\n}"); });
    CHECK(err.find("test.bent:2:13") != std::string::npos && err.find("expected ':'") != std::string::npos);
    CHECK(errorOf([] { (void)load("let x = (1 + \n"); }).find("end of input") != std::string::npos);
    CHECK(errorOf([] { (void)load("fn f(a) = a\nfn f(b) = b"); }).find("redefinition") != std::string::npos);
    CHECK(errorOf([] { (void)evalText("nosuch(1)"); }).find("unknown name 'nosuch'") != std::string::npos);
}

void testEvaluator() {
    const std::unique_ptr<Program> program = load(R"(
fn fact(n) = if n <= 1 then 1 else n * fact(n - 1)
fn compose(f, g) = fn(x) => f(g(x))
let double = fn(x) => x * 2
let inc = fn(x) => x + 1
fn even(n) = if n == 0 then true else odd(n - 1)
fn odd(n) = if n == 0 then false else even(n - 1)
)");
    CHECK(equal(evalIn(*program, "fact(10)"), Value::integer(3628800)));
    CHECK(equal(evalIn(*program, "compose(double, inc)(3)"), Value::integer(8)));
    CHECK(equal(evalIn(*program, "map([1, 2, 3], double)"), evalIn(*program, "[2, 4, 6]")));
    CHECK(equal(evalIn(*program, "filter(range(10), fn(x) => x % 3 == 0)"), evalIn(*program, "[0, 3, 6, 9]")));
    CHECK(equal(evalIn(*program, "fold(range(5), 0, fn(a, x) => a + x)"), Value::integer(10)));
    CHECK(equal(evalIn(*program, "even(40)"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "let a = 2 in let b = a * a in b + a"), Value::integer(6)));
    CHECK(equal(evalIn(*program, "forall x in [1, 2, 3]: x > 0"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "exists x in [1, 2, 3]: x > 2"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "1.0 ~= 1.0000001"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "vec3(1, 2, 3) ~= vec3(1, 2, 3.01) within 0.1"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "quat(1, 0, 0, 0) ~= quat(-1, 0, 0, 0)"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "1 == 1.0"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "nan == nan"), Value::boolean(false)));
    CHECK(equal(evalIn(*program, "rotate(axisAngle(vec3(0, 1, 0), 90), vec3(1, 0, 0)) ~= vec3(0, 0, -1)"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "with({a: 1}, b: 2)"), evalIn(*program, "{a: 1, b: 2}")));
    CHECK(errorOf([&] { (void)evalIn(*program, "fact(100000)"); }).find("recursion too deep") != std::string::npos);
    CHECK(errorOf([&] { (void)evalIn(*program, "1 + \"a\""); }).find("cannot apply '+'") != std::string::npos);
    CHECK(errorOf([&] { (void)evalIn(*program, "value()"); }).find("needs a world") != std::string::npos);
    CHECK(errorOf([&] { (void)evalIn(*program, "9223372036854775807 + 1"); }).find("overflow") != std::string::npos);
}

void testPrintRoundTrip() {
    // Every printed value parses back to an equal value (this is what makes counterexamples runnable).
    const std::unique_ptr<Program> program = load(R"(
let leaf = oneof(int(-1000000, 1000000), nasty(float(-1e9, 1e9)), text(0, 8), bool(), unitVec3(), unitQuat(), entity(int(0, 9)), add(n: int(0, 5)))
let anyValue = oneof(leaf, listOf(leaf, 0, 4), {a: leaf, b: listOf(leaf, 0, 3), "odd key": leaf})
)");
    for (uint64_t seed = 0; seed < 300; ++seed) {
        const Value v = evalIn(*program, "anyValue", seed);
        const Value back = evalIn(*program, print(v));
        const bool same = equal(v, back) || print(v) == print(back);  // NaN != NaN but prints the same
        if (!same) std::fprintf(stderr, "roundtrip mismatch: %s vs %s\n", print(v).c_str(), print(back).c_str());
        CHECK(same);
    }
}

void testGenerators() {
    const std::unique_ptr<Program> program = load("let g = listOf({x: int(-5, 5), f: float(0, 1)}, 2, 6)");
    const Value a = evalIn(*program, "g", 42);
    const Value b = evalIn(*program, "g", 42);
    CHECK(equal(a, b));
    for (uint64_t seed = 0; seed < 200; ++seed) {
        const Value v = evalIn(*program, "g", seed);
        CHECK(v.items().size() >= 2 && v.items().size() <= 6);
        for (const Value& item : v.items()) {
            const int64_t x = item.field("x")->i;
            const double f = item.field("f")->f;
            CHECK(x >= -5 && x <= 5 && f >= 0.0 && f <= 1.0);
        }
    }
    // Choice 0 everywhere gives the simplest value.
    Interp in(*program);
    Tape zero = Tape::replay({});
    in.tape = &zero;
    const Value simplest = in.realize(program->globals.at("g"), SrcLoc{});
    CHECK(print(simplest) == "[{f: 0.0, x: 0}, {f: 0.0, x: 0}]");
    // nasty() reaches non-finite values; filter and bind compose.
    bool sawNaN = false;
    for (uint64_t seed = 0; seed < 400 && !sawNaN; ++seed) sawNaN = std::isnan(evalText("nasty(float(0, 1))").f) || std::isnan(evalIn(*program, "nasty(float(0, 1))", seed).f);
    CHECK(sawNaN);
    const std::unique_ptr<Program> composed = load("let pair = bind(int(1, 5), fn(n) => listOf(just(n), n, n))\nlet odd = filter(int(0, 100), fn(x) => x % 2 == 1)");
    for (uint64_t seed = 0; seed < 50; ++seed) {
        const Value pair = evalIn(*composed, "pair", seed);
        CHECK(static_cast<int64_t>(pair.items().size()) == pair.items().front().i);
        CHECK(evalIn(*composed, "odd", seed).i % 2 == 1);
    }
}

LawReport check(const std::string& source, const std::string& law, int64_t cases = 200, int jobs = 2) {
    static std::vector<std::unique_ptr<Program>> keep;
    keep.push_back(load(source));
    CheckOptions options;
    options.cases = cases;
    options.seed = 7;
    options.jobs = jobs;
    return checkLaw(*keep.back(), *keep.back()->law(law), options, 0.0);
}

void testShrinking() {
    const LawReport intReport = check("law small { forall (x: int(0, 100000)) => x < 1234 }", "small");
    CHECK(intReport.status == "fail");
    CHECK(intReport.shrunk.bindings.size() == 1 && equal(intReport.shrunk.bindings[0].second, Value::integer(1234)));

    const LawReport listReport = check("law sum { forall (xs: listOf(int(0, 100), 0, 50)) => sum(xs) < 100 }", "sum");
    CHECK(listReport.status == "fail");
    CHECK(print(listReport.shrunk.bindings[0].second) == "[100]");

    const LawReport memberReport = check("law no7 { forall (xs: listOf(int(-50, 50), 0, 30)) => not contains(xs, 7) }", "no7", 400);
    CHECK(memberReport.status == "fail" && print(memberReport.shrunk.bindings[0].second) == "[7]");

    const LawReport floatReport = check("law f { forall (x: float(-1000, 1000)) => x < 10.0 }", "f");
    CHECK(floatReport.status == "fail" && floatReport.shrunk.bindings[0].second.f >= 10.0 && floatReport.shrunk.bindings[0].second.f <= 10.5);

    // Action removal: only the add that makes the counter large survives.
    const LawReport actionReport = check("law bounded { forall (p: listOf(oneof(add(n: int(0, 3)), wait(n: int(0, 3)), add(n: int(10, 20))), 0, 20)) => after(p) { value() < 10 } }", "bounded");
    CHECK(actionReport.status == "fail");
    CHECK(print(actionReport.shrunk.bindings[0].second) == "[add(n: 10)]");

    // Parallel and serial runs find the same first failure and the same minimal counterexample.
    const LawReport serial = check("law small { forall (x: int(0, 100000), y: int(0, 9)) => x < 5000 or y != 3 }", "small", 300, 1);
    const LawReport parallel = check("law small { forall (x: int(0, 100000), y: int(0, 9)) => x < 5000 or y != 3 }", "small", 300, 4);
    CHECK(serial.failingCase == parallel.failingCase && serial.failingCase >= 0);
    CHECK(print(serial.shrunk.bindings[0].second) == print(parallel.shrunk.bindings[0].second) && print(serial.shrunk.bindings[0].second) == "5000");
}

void testTemporal() {
    const std::unique_ptr<Program> program = load("");
    CHECK(equal(evalIn(*program, "after([add(n: 2), wait(n: 3), add(n: 1)]) { value() == 3 and now() == 3 }"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "always after([add(n: 1), wait(n: 5)]) { value() < 2 }"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "always after([add(n: 1), wait(n: 5), add(n: 1)]) { value() < 2 }"), Value::boolean(false)));
    CHECK(equal(evalIn(*program, "eventually within 10 after([add(n: 1)]) { now() >= 7 }"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "eventually within 3 after([add(n: 1)]) { now() >= 7 }"), Value::boolean(false)));
    CHECK(equal(evalIn(*program, "always within 4 after([]) { now() <= 4 }"), Value::boolean(true)));
    CHECK(equal(evalIn(*program, "after([add(n: 1)]) { value() } + after([add(n: 5)]) { value() }"), Value::integer(6)));  // worlds are independent
    CHECK(errorOf([&] { (void)evalIn(*program, "after([1]) { true }"); }).find("not an action") != std::string::npos);
}

void testDiscardsAndExamples() {
    const LawReport gaveUp = check("law never { forall (x: int(0, 10)) where x > 100 => false }", "never", 50);
    CHECK(gaveUp.status == "gave-up" && gaveUp.discards == 50);
    const LawReport filtered = check("law odd { forall (x: filter(int(0, 1000), fn(v) => v % 2 == 1)) => x % 2 == 1 }", "odd", 100);
    CHECK(filtered.status == "pass" && filtered.passed == 100);

    const std::unique_ptr<Program> program = load(R"(
law positive { forall (x: int(-5, 5)) => x >= 0 }
example positive { x = 3 }
example positive known "negative input" { x = -2 }
example positive { x = -1 }
)");
    CheckOptions options;
    const std::vector<ExampleReport> reports = runExamples(*program, options);
    CHECK(reports.size() == 3);
    CHECK(reports[0].verdict == "pass" && reports[1].verdict == "known-fail" && reports[2].verdict == "fail");
    CHECK(reports[1].reason == "negative input");
}

void testCounterexampleProgram() {
    // The regression text a failure writes is itself a loadable program whose example fails the same way.
    const std::string lawFile = testsupport::tempFilePath("bent_", "law.bent");
    {
        std::ofstream out(lawFile);
        out << "law bounded { forall (p: listOf(oneof(add(n: int(0, 3)), add(n: int(10, 20))), 0, 20)) => after(p) { value() < 10 } }\n";
    }
    const std::unique_ptr<Program> program = loadProgram(lawFile, &gHost);
    CheckOptions options;
    options.cases = 200;
    options.regressionDir = std::filesystem::path(lawFile).parent_path().string();
    const LawReport report = checkLaw(*program, *program->law("bounded"), options, 0.0);
    CHECK(report.status == "fail" && !report.regressionPath.empty());
    const std::unique_ptr<Program> regression = loadProgram(report.regressionPath, &gHost);
    CHECK(regression->examples.size() == 1);
    const std::vector<ExampleReport> examples = runExamples(*regression, options);
    CHECK(examples.size() == 1 && examples[0].verdict == "fail");
    std::filesystem::remove(report.regressionPath);
    std::filesystem::remove(lawFile);
}

void testIsolation() {
    // A crashing engine call is caught in a child process and shrunk like any failure.
    const std::unique_ptr<Program> program = load("law survives { forall (p: listOf(oneof(add(n: int(0, 3)), crash()), 0, 6)) => after(p) { true } }");
    CheckOptions options;
    options.cases = 50;
    options.isolate = true;
    options.jobs = 2;
    const LawReport report = checkLaw(*program, *program->law("survives"), options, 0.0);
    CHECK(report.status == "error");
    CHECK(report.shrunk.message.find("crashed") != std::string::npos);
    CHECK(print(report.shrunk.bindings[0].second) == "[crash()]");
}

}

int main() {
    testLexer();
    testParser();
    testEvaluator();
    testPrintRoundTrip();
    testGenerators();
    testShrinking();
    testTemporal();
    testDiscardsAndExamples();
    testCounterexampleProgram();
    testIsolation();
    return testsupport::finish();
}
