#include "TestSupport.hpp"
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.glm;
import Engine.camera;
import Engine.Render.settings;
import Engine.TestScenes;

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr float kStep = 1.0f / 60.0f;

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    [[nodiscard]] bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
    [[nodiscard]] glm::ivec3 at(int x, int y) const {
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        return glm::ivec3(rgba[index], rgba[index + 1], rgba[index + 2]);
    }
};

Image loadImage(const std::string& path) {
    Image image;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &image.width, &image.height, &channels, 4);
    if (!pixels) return image;
    image.rgba.assign(pixels, pixels + static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
    stbi_image_free(pixels);
    return image;
}

struct Diff {
    double mean = 0.0;
    double fraction = 0.0;
};

Diff compare(const Image& a, const Image& b) {
    Diff diff;
    if (!a.valid() || !b.valid() || a.width != b.width || a.height != b.height) {
        diff.mean = 1.0e9;
        diff.fraction = 1.0;
        return diff;
    }
    double sum = 0.0;
    size_t over = 0;
    const size_t pixels = static_cast<size_t>(a.width) * static_cast<size_t>(a.height);
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        int worst = 0;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = std::abs(static_cast<int>(a.rgba[pixel * 4 + channel]) - static_cast<int>(b.rgba[pixel * 4 + channel]));
            sum += delta;
            worst = std::max(worst, delta);
        }
        if (worst > 8) ++over;
    }
    diff.mean = sum / static_cast<double>(pixels * 3);
    diff.fraction = static_cast<double>(over) / static_cast<double>(pixels);
    return diff;
}

std::string shotPath(const std::string& name) { return (std::filesystem::temp_directory_path() / ("lit_largeworld_" + name + ".png")).string(); }

int gFrame = 0;

void frame(Engine& engine, const std::string& shot = {}) {
    if (!shot.empty()) engine.requestScreenshot(shot);
    ++gFrame;
    engine.tick(kStep);
    engine.update();
    engine.present();
}

Image capture(Engine& engine, const std::string& name) {
    const std::string path = shotPath(name);
    std::filesystem::remove(path);
    frame(engine, path);
    return loadImage(path);
}

bool project(Engine& engine, const glm::dvec3& world, glm::ivec2& pixel) {
    const auto screen = engine.worldToScreen(engine.world().toLocal(world));
    if (!screen) return false;
    pixel = glm::ivec2(static_cast<int>(screen->x), static_cast<int>(screen->y));
    return pixel.x >= 2 && pixel.y >= 2 && pixel.x < kWidth - 2 && pixel.y < kHeight - 2;
}

glm::dvec3 sceneBase(double originKm) { return glm::dvec3(originKm * 1000.0, 0.0, -0.5 * originKm * 1000.0); }

double redness(const Image& image, glm::ivec2 pixel) {
    double sum = 0.0;
    int count = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const glm::ivec3 c = image.at(pixel.x + dx, pixel.y + dy);
            sum += c.r - c.b;
            ++count;
        }
    }
    return sum / count;
}

void testDepthAndRebase(Engine& engine, double originKm) {
    for (int i = 0; i < 60; ++i) frame(engine);
    const glm::dvec3 base = sceneBase(originKm);
    const Image control1 = capture(engine, "control1");
    const Image control2 = capture(engine, "control2");
    CHECK(control1.valid() && control2.valid());
    if (!control2.valid()) return;

    glm::ivec2 front50, back50, front100, back100;
    const bool visible = project(engine, base + glm::dvec3(3000.0, 7000.0, -49960.0), front50) && project(engine, base + glm::dvec3(18000.0, 7000.0, -50040.0), back50) &&
                         project(engine, base + glm::dvec3(45000.0, 18000.0, -99960.0), front100) && project(engine, base + glm::dvec3(60000.0, 18000.0, -100160.0), back100);
    CHECK(visible);
    if (visible) {
        const double r50 = redness(control2, front50) - redness(control2, back50);
        const double r100 = redness(control2, front100) - redness(control2, back100);
        std::printf("depth precision: red-blue contrast 50 km %.1f, 100 km %.1f (front px %d,%d back px %d,%d)\n", r50, r100, front50.x, front50.y, back50.x, back50.y);
        CHECK(r50 > 25.0);
        CHECK(r100 > 8.0);
    }

    const glm::dvec3 cameraBefore = engine.cameraWorldPosition();
    const double epochBefore = static_cast<double>(engine.originEpoch());
    CHECK(engine.rebaseOrigin(glm::dvec3(1024.0, 256.0, -2048.0)));
    CHECK(static_cast<double>(engine.originEpoch()) == epochBefore + 1.0);
    const glm::dvec3 cameraAfter = engine.cameraWorldPosition();
    CHECK(glm::length(cameraAfter - cameraBefore) < 1.0e-3);
    const Image afterFirst = capture(engine, "rebase_first");
    const Image afterSecond = capture(engine, "rebase_second");
    const Diff noise = compare(control1, control2);
    const Diff first = compare(control2, afterFirst);
    const Diff second = compare(control2, afterSecond);
    std::printf("rebase diff: control mean %.4f frac %.5f | first frame after rebase mean %.4f frac %.5f | second mean %.4f frac %.5f\n", noise.mean, noise.fraction, first.mean, first.fraction, second.mean, second.fraction);
    CHECK(first.mean <= noise.mean + 0.15);
    CHECK(first.fraction <= noise.fraction + 0.0015);
    CHECK(second.mean <= noise.mean + 0.15);
    CHECK(second.fraction <= noise.fraction + 0.0015);
    glm::ivec2 front, back;
    if (project(engine, base + glm::dvec3(3000.0, 7000.0, -49960.0), front) && project(engine, base + glm::dvec3(18000.0, 7000.0, -50040.0), back)) CHECK(redness(afterSecond, front) - redness(afterSecond, back) > 25.0);
}

void testFlight(Engine& engine) {
    setLargeWorldFlight(engine, 6.0f);
    std::vector<double> regular;
    std::vector<double> atRebase;
    Image previous = capture(engine, "fly_0");
    uint64_t epoch = engine.originEpoch();
    const double startX = engine.cameraWorldPosition().x;
    for (int step = 1; step <= 130; ++step) {
        const Image current = capture(engine, "fly_" + std::to_string(step % 2));
        const Diff diff = compare(previous, current);
        const uint64_t now = engine.originEpoch();
        (now != epoch ? atRebase : regular).push_back(diff.mean);
        epoch = now;
        previous = current;
    }
    setLargeWorldFlight(engine, 0.0f);
    const double flown = engine.cameraWorldPosition().x - startX;
    CHECK(flown > 10.0);
    CHECK(!atRebase.empty());
    CHECK(regular.size() > 20);
    if (atRebase.empty() || regular.empty()) return;
    std::sort(regular.begin(), regular.end());
    const double p90 = regular[regular.size() * 9 / 10];
    const double worstRebase = *std::max_element(atRebase.begin(), atRebase.end());
    std::printf("flight: %.0f m flown, %zu rebases, frame diff regular p90 %.3f max %.3f, at rebase max %.3f\n", flown, atRebase.size(), p90, regular.back(), worstRebase);
    CHECK(worstRebase <= regular.back() * 1.3 + 0.2);
    CHECK(atRebase.size() >= 2);
    CHECK(engine.world().camera().getPosition().x < 100.0f);
}

}

int main() {
    if (!glfwInit()) {
        std::printf("GLFW unavailable, skipping large world render tests\n");
        return 0;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(kWidth, kHeight, "Lit Large World Render Tests", nullptr, nullptr);
    if (!window) {
        std::printf("No window available, skipping large world render tests\n");
        glfwTerminate();
        return 0;
    }
    {
        Engine engine;
        engine.init(window, kWidth, kHeight);
        LargeWorldConfig config;
        config.rebaseThreshold = 3.0;
        config.rebaseSnap = 4.0;
        buildLargeWorldScene(engine, config);
        testDepthAndRebase(engine, config.originKm);
        testFlight(engine);
        engine.cleanup();
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return testsupport::finish();
}
