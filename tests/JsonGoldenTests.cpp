#include "TestSupport.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

import Engine.Json;
import Engine.Rng;

namespace {

using Ordered = nlohmann::ordered_json;

constexpr int kRandomDocuments = 300;
constexpr int kMaxDepth = 4;
constexpr uint32_t kChildChoices = 6;
constexpr uint32_t kKindChoices = 9;
constexpr int kPrettyIndent = 2;

Ordered referenceOrdered(const Json& json) {
    switch (json.type()) {
        case Json::Type::Null: return nullptr;
        case Json::Type::Bool: return json.asBool();
        case Json::Type::Number: return std::isfinite(json.asNumber()) ? Ordered(json.asNumber()) : Ordered(nullptr);
        case Json::Type::String: return json.asString();
        case Json::Type::Array: {
            Ordered array = Ordered::array();
            for (const Json& item : json.items()) array.push_back(referenceOrdered(item));
            return array;
        }
        case Json::Type::Object: {
            Ordered object = Ordered::object();
            for (const auto& [key, value] : json.members()) object[key] = referenceOrdered(value);
            return object;
        }
    }
    return nullptr;
}

Json referenceFrom(const Ordered& ordered) {
    if (ordered.is_boolean()) return Json(ordered.get<bool>());
    if (ordered.is_number()) return Json(ordered.get<double>());
    if (ordered.is_string()) return Json(ordered.get<std::string>());
    if (ordered.is_array()) {
        Json array = Json::array();
        for (const Ordered& item : ordered) array.push(referenceFrom(item));
        return array;
    }
    if (ordered.is_object()) {
        Json object = Json::object();
        for (const auto& [key, value] : ordered.items()) object.set(key, referenceFrom(value));
        return object;
    }
    return Json();
}

std::string referenceDump(const Json& json, int indent) { return referenceOrdered(json).dump(indent, ' ', false, Ordered::error_handler_t::replace); }

std::string randomString(Pcg32& rng) {
    static const char* const pieces[] = {"a", "Z", " ", "\"", "\\", "\n", "\t", "\x01", "\x1f", "/", "\x7f", "\xc3\xa9", "\xe2\x82\xac", "\xff", "long_name_segment"};
    std::string text;
    const uint32_t count = rng.below(8);
    for (uint32_t index = 0; index < count; ++index) text += pieces[rng.below(sizeof(pieces) / sizeof(pieces[0]))];
    return text;
}

double randomNumber(Pcg32& rng) {
    switch (rng.below(6)) {
        case 0: return static_cast<double>(rng.below(1000)) - 500.0;
        case 1: return static_cast<double>(rng.uniform() * 400.0f);
        case 2: return (static_cast<double>(rng.uniform()) - 0.5) * 1.0e-7;
        case 3: return (static_cast<double>(rng.uniform()) - 0.5) * 1.0e22;
        case 4: return std::numeric_limits<double>::infinity();
        default: return static_cast<double>(rng.next()) * 4294967296.0 + static_cast<double>(rng.next());
    }
}

Json randomJson(Pcg32& rng, int depth) {
    const uint32_t kind = rng.below(depth >= kMaxDepth ? 5 : kKindChoices);
    switch (kind) {
        case 0: return Json();
        case 1: return Json(rng.below(2) == 0);
        case 2: return Json(randomNumber(rng));
        case 3: case 4: return Json(randomString(rng));
        case 5: case 6: {
            Json array = Json::array();
            const uint32_t count = rng.below(kChildChoices);
            for (uint32_t index = 0; index < count; ++index) array.push(randomJson(rng, depth + 1));
            return array;
        }
        default: {
            Json object = Json::object();
            const uint32_t count = rng.below(kChildChoices);
            for (uint32_t index = 0; index < count; ++index) object.set(randomString(rng) + std::to_string(index), randomJson(rng, depth + 1));
            return object;
        }
    }
}

void testGoldenText() {
    Json scene = Json::object();
    scene.set("version", 3);
    scene.set("name", "a\"b\\c\n");
    scene.set("empty", Json::array());
    scene.set("none", Json::object());
    Json list = Json::array();
    list.push(1.5f);
    list.push(true);
    list.push(nullptr);
    list.push(-0.0);
    scene.set("list", std::move(list));
    CHECK(scene.dump() == R"({"version":3.0,"name":"a\"b\\c\n","empty":[],"none":{},"list":[1.5,true,null,-0.0]})");
    CHECK(scene.dump(2) == "{\n  \"version\": 3.0,\n  \"name\": \"a\\\"b\\\\c\\n\",\n  \"empty\": [],\n  \"none\": {},\n  \"list\": [\n    1.5,\n    true,\n    null,\n    -0.0\n  ]\n}");
    CHECK(scene.dump(0) == referenceDump(scene, 0));
}

void testRandomMatchesReference() {
    Pcg32 rng(0x6a09e667f3bcc908ULL);
    int mismatches = 0;
    for (int document = 0; document < kRandomDocuments; ++document) {
        const Json json = randomJson(rng, 0);
        const std::string compact = json.dump();
        const std::string pretty = json.dump(kPrettyIndent);
        if (compact != referenceDump(json, -1) || pretty != referenceDump(json, kPrettyIndent)) ++mismatches;
        const auto reparsed = Json::parse(compact);
        const Json expected = referenceFrom(Ordered::parse(compact));
        if (!reparsed || !(*reparsed == expected)) ++mismatches;
        if (reparsed && reparsed->dump() != compact) ++mismatches;
        const auto prettyParsed = Json::parse(pretty);
        if (!prettyParsed || !(*prettyParsed == expected)) ++mismatches;
    }
    CHECK(mismatches == 0);
}

void testFloatBytes() {
    Pcg32 rng(42);
    int mismatches = 0;
    for (int sample = 0; sample < 200000; ++sample) {
        const float value = (rng.uniform() - 0.5f) * 1000.0f;
        Json array = Json::array();
        array.push(value);
        if (array.dump() != referenceDump(array, -1)) ++mismatches;
    }
    CHECK(mismatches == 0);
}

void testParseSemantics() {
    const auto duplicate = Json::parse(R"({"a":1,"b":2,"a":3})");
    CHECK(duplicate.has_value());
    CHECK(duplicate->members().size() == 2);
    CHECK(duplicate->members()[0].first == "a" && duplicate->get("a")->asNumber() == 3.0);
    std::string text = "{";
    for (int index = 0; index < 100; ++index) text += "\"k" + std::to_string(index % 60) + "\":" + std::to_string(index) + (index < 99 ? "," : "");
    text += "}";
    const auto wide = Json::parse(text);
    CHECK(wide.has_value() && wide->size() == 60);
    CHECK(wide->get("k0")->asNumber() == 60.0);
    CHECK(wide->members()[59].first == "k59");
    CHECK(Json::parse("7")->asNumber() == 7.0);
    CHECK(Json::parse(" [ ] ")->isArray());
    std::string error;
    CHECK(!Json::parse("[1,", &error).has_value());
    CHECK(error.find("parse_error") != std::string::npos);
    CHECK(Json("bad\xff").dump() == referenceDump(Json("bad\xff"), -1));
}

}

int main() {
    testGoldenText();
    testRandomMatchesReference();
    testFloatBytes();
    testParseSemantics();
    return testsupport::finish();
}
