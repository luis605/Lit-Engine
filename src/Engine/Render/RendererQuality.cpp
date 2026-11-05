module;

#include "RendererPrelude.hpp"

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.governor;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

constexpr float kMillisecondsPerSecond = 1000.0f;

int scaledDimension(int windowSize, float scale) { return std::max(static_cast<int>(std::lround(static_cast<float>(windowSize) * scale)), 1); }

}

bool Renderer::applyRenderSize() {
    const float scale = quantizeRenderScale(m_lighting.settings().renderScale);
    const int width = scaledDimension(m_windowWidth, scale);
    const int height = scaledDimension(m_windowHeight, scale);
    m_diligent->pImmediateContext->WaitForIdle();
    m_renderWidth = width;
    m_renderHeight = height;
    createScreenTargets();
    m_lighting.resize(width, height, m_windowWidth, m_windowHeight);
    return true;
}

void Renderer::updateQualityScale(float deltaTime) {
    if (!m_diligent || !m_initialized) return;
    RenderSettings& settings = m_lighting.settings();
    const double gpuMilliseconds = m_diligent->gpuTotalMilliseconds;
    const float frameMilliseconds = gpuMilliseconds > 0.0 ? static_cast<float>(gpuMilliseconds) : deltaTime * kMillisecondsPerSecond;
    updateGovernor(m_governor, settings, frameMilliseconds, deltaTime);
    const float scale = quantizeRenderScale(settings.renderScale);
    if (scaledDimension(m_windowWidth, scale) != m_renderWidth || scaledDimension(m_windowHeight, scale) != m_renderHeight) applyRenderSize();
}
