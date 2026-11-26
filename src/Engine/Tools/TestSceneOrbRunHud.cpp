module;

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.OrbRun;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Portal;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.OrbRunRules;

#include "OrbRunInternal.hpp"

namespace orbrun {

namespace {

constexpr float kGlyphAdvance = 26.0f;
constexpr float kMargin = 24.0f;
constexpr float kHudScale = 0.6f;
constexpr float kClockScale = 0.9f;
constexpr float kBannerScale = 1.8f;
constexpr float kSubtitleScale = 0.7f;
constexpr float kBannerY = 260.0f;
constexpr float kSubtitleY = 350.0f;
constexpr float kHintBottomOffset = 60.0f;
constexpr float kHintSeconds = 14.0f;
constexpr float kWarningSeconds = 30.0f;
constexpr float kCaughtFlashSeconds = 1.2f;
constexpr float kCaughtY = 200.0f;

const glm::vec3 kGold(1.0f, 0.82f, 0.3f);
const glm::vec3 kWhite(0.95f, 0.95f, 0.95f);
const glm::vec3 kRed(1.0f, 0.25f, 0.2f);
const glm::vec3 kGreen(0.5f, 1.0f, 0.5f);

float textWidth(const std::string& text, float scale) { return static_cast<float>(text.size()) * kGlyphAdvance * scale; }

void drawCentered(Engine& engine, const std::string& text, float y, float scale, const glm::vec3& color) {
    engine.AddText(text, (engine.windowSize().x - textWidth(text, scale)) * 0.5f, y, scale, color);
}

void drawRightAligned(Engine& engine, const std::string& text, float y, float scale, const glm::vec3& color) {
    engine.AddText(text, engine.windowSize().x - kMargin - textWidth(text, scale), y, scale, color);
}

void drawStatus(Engine& engine, const OrbRunGame& game) {
    const OrbRunState& state = game.state;
    engine.AddText("ORBS " + std::to_string(state.collected) + "/" + std::to_string(game.config.orbTotal), kMargin, kMargin, kHudScale, kGold);
    const bool urgent = state.timeLeft < kWarningSeconds;
    drawCentered(engine, orbRunClockText(state.timeLeft), kMargin, kClockScale, urgent ? kRed : kWhite);
    drawRightAligned(engine, "LIVES " + std::to_string(state.lives), kMargin, kHudScale, state.lives > 1 ? kGreen : kRed);
}

void drawEvents(Engine& engine, const OrbRunGame& game) {
    const OrbRunState& state = game.state;
    const float flashStart = game.config.invulnerableSeconds - kCaughtFlashSeconds;
    if (state.invulnerable > flashStart && game.spiderHits > 0) drawCentered(engine, "CAUGHT BY A SPIDER", kCaughtY, kSubtitleScale, kRed);
    if (state.elapsed < kHintSeconds) drawCentered(engine, "WASD move   Space jump   Shift sprint   E shove crate   R restart   spiders freeze when you look at them", engine.windowSize().y - kHintBottomOffset, 0.45f, kWhite);
}

void drawEnding(Engine& engine, const OrbRunGame& game) {
    const OrbRunState& state = game.state;
    if (state.phase == OrbRunPhase::Won) {
        drawCentered(engine, "YOU WIN", kBannerY, kBannerScale, kGold);
        drawCentered(engine, "all orbs collected in " + orbRunClockText(game.config.timeLimit - state.timeLeft) + "   press R to play again", kSubtitleY, kSubtitleScale, kWhite);
    } else if (state.phase == OrbRunPhase::Lost) {
        drawCentered(engine, state.loss == OrbRunLoss::Timeout ? "TIME IS UP" : "OUT OF LIVES", kBannerY, kBannerScale, kRed);
        drawCentered(engine, std::to_string(state.collected) + " of " + std::to_string(game.config.orbTotal) + " orbs   press R to try again", kSubtitleY, kSubtitleScale, kWhite);
    }
}

}

void drawHud(Engine& engine, const OrbRunGame& game) {
    drawStatus(engine, game);
    drawEvents(engine, game);
    drawEnding(engine, game);
}

}
