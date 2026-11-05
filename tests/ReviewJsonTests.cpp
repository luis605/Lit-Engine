#include "ReviewSuites.hpp"
#include "TestSupport.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

import Engine.Json;
import Engine.Rng;

namespace {

constexpr size_t kHugeDepth = 400000;
constexpr int kFuzzRounds = 20000;
constexpr double kMaxFuzzSeconds = 60.0;

const char* const kSeedDocuments[] = {
    R"({"name":"spider","legs":[1,2,3.5e2,-4.25],"nested":{"a":true,"b":null,"c":"x\u00e9\n\"y\""},"empty":{},"list":[]})",
    R"([[],[[]],{"k":[{"z":1e-7}]},"\ud83d\ude00",12345678901234567890,-0.0,1E+2])",
    R"({"a":1,"a":2,"b":{"c":1,"c":3}})",
    R"(  {"deep":[[[[[[[[[[1]]]]]]]]]]}  )",
};

std::string mutate(const std::string& source, Pcg32& rng) {
    std::string text = source;
    const uint32_t edits = 1 + rng.next() % 4;
    for (uint32_t edit = 0; edit < edits && !text.empty(); ++edit) {
        const size_t position = rng.next() % text.size();
        switch (rng.next() % 6) {
            case 0: text[position] = static_cast<char>(rng.next() & 0xFF); break;
            case 1: text.erase(position, 1 + rng.next() % 3); break;
            case 2: text.insert(position, std::string(1, "[]{}\",:\\u-+.eE0123456789tfn"[rng.next() % 28])); break;
            case 3: text.resize(position); break;
            case 4: text.insert(position, "\\ud83d"); break;
            default: text.insert(position, std::string(1 + rng.next() % 40, "[{"[rng.next() % 2])); break;
        }
    }
    return text;
}

void testDeepNestingDoesNotCrash() {
    std::string arrays(kHugeDepth, '[');
    arrays.append(kHugeDepth, ']');
    std::string error;
    const auto parsedArrays = Json::parse(arrays, &error);
    CHECK(!parsedArrays.has_value());
    CHECK(!error.empty());

    std::string objects;
    for (size_t level = 0; level < kHugeDepth; ++level) objects += "{\"a\":";
    objects += "1";
    objects.append(kHugeDepth, '}');
    CHECK(!Json::parse(objects).has_value());

    std::string moderate(100, '[');
    moderate.append(100, ']');
    const auto accepted = Json::parse(moderate);
    CHECK(accepted.has_value());
    if (accepted) CHECK(accepted->dump() == moderate);
}

void testEdgeInputs() {
    CHECK(!Json::parse("").has_value());
    CHECK(!Json::parse("   ").has_value());
    CHECK(!Json::parse("[1,]").has_value());
    CHECK(!Json::parse("{\"a\":1,}").has_value());
    CHECK(!Json::parse("[1] x").has_value());
    CHECK(!Json::parse("\"\\ud800\"").has_value());
    CHECK(!Json::parse("\"\\u12\"").has_value());
    CHECK(!Json::parse("nan").has_value());
    CHECK(!Json::parse("[01]").has_value());
    CHECK(!Json::parse("[1,2").has_value());
    CHECK(Json::parse("\"\\u00e9\"").has_value());
    const auto duplicate = Json::parse(R"({"a":1,"b":2,"a":3})");
    CHECK(duplicate && duplicate->size() == 2 && duplicate->get("a")->asNumber() == 3.0);
    const auto huge = Json::parse("[1e999,-1e999,123456789012345678901234567890]");
    if (huge) {
        const std::string dumped = huge->dump();
        CHECK(Json::parse(dumped).has_value());
    }
    const auto nulled = Json::parse(std::string("[\"a\\u0000b\"]"));
    CHECK(nulled && nulled->items()[0].asString().size() == 3);
    CHECK(Json::parse(nulled->dump()).has_value());
    CHECK(Json::parse(std::string_view("[1,2]\0garbage", 13)) == std::nullopt);
}

void testFuzzMutations() {
    Pcg32 rng(20240601);
    const auto start = std::chrono::steady_clock::now();
    int accepted = 0;
    for (int round = 0; round < kFuzzRounds; ++round) {
        const std::string text = mutate(kSeedDocuments[rng.next() % 4], rng);
        std::string error;
        const auto parsed = Json::parse(text, &error);
        if (!parsed) continue;
        ++accepted;
        const std::string dumped = parsed->dump();
        const auto reparsed = Json::parse(dumped);
        CHECK(reparsed.has_value());
        if (reparsed) CHECK(jsonDiff(*parsed, *reparsed, 0.0).empty());
        (void)parsed->dump(2);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < kMaxFuzzSeconds);
    CHECK(accepted > 0);
}

}

void runReviewJsonTests() {
    testEdgeInputs();
    testFuzzMutations();
    testDeepNestingDoesNotCrash();
}
