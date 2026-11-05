# GI color bleeding

Indirect diffuse light picks up the color of nearby lit surfaces (a red wall tints a nearby white floor). It is a spatial-only screen-space one-bounce gather that lives in `resources/shaders/lighting/gtao.comp`, next to the existing SSGI term, and rides on the existing bilateral GTAO blur. There is no temporal accumulation: each pixel takes a fixed, per-pixel rotated tap pattern and the result is cleaned up by the spatial depth-aware blur only.

How it works
- Each half-resolution GTAO pixel takes N golden-angle spiral taps inside a world-space radius.
- A tap contributes the lit color (mip 2 of the scene color pyramid) weighted by receiver cosine, emitter cosine and a smooth distance falloff. Lit color already contains emitter albedo times its irradiance, so the gathered light is tinted by the emitter's albedo.
- The gather is slightly saturated to counter the averaging toward grey, then added to the GTAO RGB output that the lighting pass multiplies by receiver albedo.
- Cost is one half-resolution pass already paid for by GTAO plus N extra taps.

Settings (RenderSettings, settings JSON, params, `set` commands, Look panel under Screen space, palette command "GI color bleeding")
- `colorBleed` on/off (default on). Requires `ssao` on; with AO off bleeding is off.
- `colorBleedIntensity` 0..4 (default 1). Scales added bounce light.
- `colorBleedRadius` 0.1..12 meters (default 2.5). Larger reaches farther walls but blurs and costs more cache misses. Capped to 160 px on screen.
- `colorBleedQuality` 0..3 selects 4, 8, 14 or 24 taps (default 1 = 8). Lower tiers are noisier but cheaper.

Tuning
- Tint too weak: raise intensity before radius; check the emitter is actually lit (bleed samples lit color).
- Noise or banding in dim areas: raise quality one tier, or lower radius.
- Interiors: radius 3 to 5 m, quality 2. Open outdoor scenes: radius 1.5 to 2.5 m, quality 0 or 1.
- Over-bright corners: lower intensity; AO already darkens contact areas.

Graceful degradation
- Needs the previous frame's lit pyramid; the first frame, portal views and headless runs without GPU skip the term (zero uniform) and render unchanged.
- Offscreen taps are skipped; only on-screen surfaces can bleed.
- Settings are clamped on the CPU (`colorBleedUniform` in `Engine.Render.settings`).
