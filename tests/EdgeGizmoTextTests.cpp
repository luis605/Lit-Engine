#include "TestSupport.hpp"
#include "EdgeSuites.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

import Engine.GizmoMath;
import Engine.LineEditor;
import Engine.World;
import Engine.glm;

namespace {

constexpr float kPi = 3.14159265f;

void testRayPlane() {
    Ray ray{{0, 5, 0}, {0, -1, 0}};
    const auto hit = gizmo::rayPlane(ray, {0, 0, 0}, {0, 1, 0});
    CHECK(hit.has_value() && std::fabs(hit->y) < 1.0e-5f);
    CHECK(!gizmo::rayPlane(ray, {0, 10, 0}, {0, 1, 0}).has_value());
    CHECK(!gizmo::rayPlane(Ray{{0, 5, 0}, {1, 0, 0}}, {0, 0, 0}, {0, 1, 0}).has_value());
    CHECK(gizmo::rayPlane(Ray{{0, 0, 0}, {0, -1, 0}}, {0, 0, 0}, {0, 1, 0}).has_value());
    CHECK(!gizmo::rayPlane(Ray{{0, 5, 0}, {0, 0, 0}}, {0, 0, 0}, {0, 1, 0}).has_value());
}

void testAxisParameter() {
    Ray ray{{3, 0, 5}, {0, 0, -1}};
    CHECK(std::fabs(gizmo::closestParameterOnAxis(ray, {0, 0, 0}, {1, 0, 0}) - 3.0f) < 1.0e-4f);
    CHECK(gizmo::closestParameterOnAxis(ray, {0, 0, 0}, {0, 0, 1}) == 0.0f);
    CHECK(gizmo::closestParameterOnAxis(ray, {0, 0, 0}, {0, 0, 0}) == 0.0f);
    CHECK(gizmo::closestParameterOnAxis(Ray{{0, 0, 0}, {0, 0, 0}}, {0, 0, 0}, {1, 0, 0}) == 0.0f);
}

void testAngles() {
    const glm::vec3 up(0, 1, 0);
    CHECK(std::fabs(gizmo::signedAngleAroundAxis({1, 0, 0}, {0, 0, -1}, up) - kPi * 0.5f) < 1.0e-4f);
    CHECK(std::fabs(gizmo::signedAngleAroundAxis({1, 0, 0}, {0, 0, 1}, up) + kPi * 0.5f) < 1.0e-4f);
    CHECK(std::fabs(std::fabs(gizmo::signedAngleAroundAxis({1, 0, 0}, {-1, 0, 0}, up)) - kPi) < 1.0e-4f);
    CHECK(gizmo::signedAngleAroundAxis({0, 1, 0}, {1, 0, 0}, up) == 0.0f);
    CHECK(gizmo::signedAngleAroundAxis({0, 0, 0}, {1, 0, 0}, up) == 0.0f);
    CHECK(gizmo::signedAngleAroundAxis({1, 0, 0}, {1, 0, 0}, up) == 0.0f);
}

void testSnapAndSegments() {
    CHECK(gizmo::snap(1.3f, 0.5f) == 1.5f);
    CHECK(gizmo::snap(-1.3f, 0.5f) == -1.5f);
    CHECK(gizmo::snap(1.3f, 0.0f) == 1.3f);
    CHECK(gizmo::snap(1.3f, -2.0f) == 1.3f);
    CHECK(gizmo::snap(0.25f, 0.5f) == 0.5f || gizmo::snap(0.25f, 0.5f) == 0.0f);

    CHECK(std::fabs(gizmo::distanceToSegment2D({0, 1}, {-1, 0}, {1, 0}) - 1.0f) < 1.0e-6f);
    CHECK(std::fabs(gizmo::distanceToSegment2D({3, 0}, {-1, 0}, {1, 0}) - 2.0f) < 1.0e-6f);
    CHECK(std::fabs(gizmo::distanceToSegment2D({3, 4}, {0, 0}, {0, 0}) - 5.0f) < 1.0e-6f);
}

void testSphereAndFraming() {
    CHECK(gizmo::enclosingSphere({}) == glm::vec4(0.0f));
    const glm::vec4 single = gizmo::enclosingSphere({glm::vec4(1, 2, 3, 4)});
    CHECK(glm::length(glm::vec3(single) - glm::vec3(1, 2, 3)) < 1.0e-5f && std::fabs(single.w - 4.0f) < 1.0e-5f);
    const glm::vec4 pair = gizmo::enclosingSphere({glm::vec4(-2, 0, 0, 1), glm::vec4(2, 0, 0, 1)});
    CHECK(std::fabs(pair.w - 3.0f) < 1.0e-5f && std::fabs(pair.x) < 1.0e-5f);

    CHECK(gizmo::frameDistance(0.0f, 60.0f, 1.0f) == 0.0f);
    CHECK(gizmo::frameDistance(1.0f, 60.0f, 0.0f) > 0.0f);
    CHECK(std::isfinite(gizmo::frameDistance(1.0f, 60.0f, 0.0f)));
    CHECK(gizmo::frameDistance(1.0f, 60.0f, 0.5f) > gizmo::frameDistance(1.0f, 60.0f, 2.0f));
    CHECK(std::isfinite(gizmo::frameDistance(1.0f, 0.0f, 1.0f)));
}

void testMatrixHelpers() {
    const glm::mat4 identity(1.0f);
    const glm::mat4 rotated = gizmo::rotateAboutWorldAxis(glm::translate(identity, glm::vec3(2, 0, 0)), {0, 0, 0}, {0, 0, 2}, kPi * 0.5f);
    CHECK(glm::length(glm::vec3(rotated[3]) - glm::vec3(0, 2, 0)) < 1.0e-4f);
    CHECK(gizmo::scaleAlongLocalAxis(identity, 1, 2.0f, false)[1][1] == 2.0f);
    CHECK(gizmo::scaleAlongLocalAxis(identity, 3, 2.0f, false) == identity);
    CHECK(gizmo::scaleAlongLocalAxis(identity, -1, 2.0f, false) == identity);
    CHECK(gizmo::scaleAlongLocalAxis(identity, 0, 3.0f, true)[2][2] == 3.0f);
}

void testLineEditorEdges() {
    LineEditor editor;
    CHECK(editor.insert("x") == EditResult::None);
    CHECK(editor.press(EditKey::Enter) == EditResult::None);

    editor.begin("ab", 4);
    CHECK(editor.insert("") == EditResult::None);
    CHECK(editor.insert("\n\t\x7f") == EditResult::None);
    CHECK(editor.insert("cde") == EditResult::None);
    CHECK(editor.insert("cd") == EditResult::Changed);
    CHECK(editor.text() == "abcd");
    CHECK(editor.insert("e") == EditResult::None);

    editor.press(EditKey::Home);
    CHECK(editor.press(EditKey::Backspace) == EditResult::None);
    editor.press(EditKey::End);
    CHECK(editor.press(EditKey::Delete) == EditResult::None);
    editor.press(EditKey::Left);
    CHECK(editor.display() == "abc|d");
    CHECK(editor.press(EditKey::Clear) == EditResult::Changed);
    CHECK(editor.press(EditKey::Clear) == EditResult::None);
    CHECK(editor.cursor() == 0);
    CHECK(editor.press(EditKey::Escape) == EditResult::Cancelled);
    CHECK(!editor.active() && editor.text() == "ab");
    CHECK(editor.display() == "ab");
}

void testLineEditorUtf8() {
    LineEditor editor;
    editor.begin("h\xC3\xA9llo", 256);
    editor.press(EditKey::Home);
    editor.press(EditKey::Right);
    editor.press(EditKey::Right);
    CHECK(editor.cursor() == 3);
    CHECK(editor.press(EditKey::Backspace) == EditResult::Changed);
    CHECK(editor.text() == "hllo");

    editor.begin("\xC3\xA9\xC3\xA9\xC3\xA9", 3);
    CHECK(editor.text() == "\xC3\xA9");
    editor.begin("abc", 0);
    CHECK(editor.text().empty() && editor.cursor() == 0);
    CHECK(editor.insert("a") == EditResult::None);

    editor.begin("foo  bar_baz qux", 256);
    editor.press(EditKey::WordLeft);
    CHECK(editor.cursor() == 13);
    editor.press(EditKey::WordLeft);
    CHECK(editor.cursor() == 5);
    editor.press(EditKey::WordLeft);
    CHECK(editor.cursor() == 0);
    editor.press(EditKey::WordLeft);
    CHECK(editor.cursor() == 0);
    editor.press(EditKey::WordRight);
    CHECK(editor.cursor() == 3);
    editor.press(EditKey::End);
    editor.press(EditKey::WordRight);
    CHECK(editor.cursor() == editor.text().size());
}

}

void runEdgeGizmoTextTests() {
    testRayPlane();
    testAxisParameter();
    testAngles();
    testSnapAndSegments();
    testSphereAndFraming();
    testMatrixHelpers();
    testLineEditorEdges();
    testLineEditorUtf8();
}
