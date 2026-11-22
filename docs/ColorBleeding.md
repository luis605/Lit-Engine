# GI color bleeding

Indirect diffuse light picks up the color of nearby lit surfaces (a red wall tints a nearby white floor). It is a spatial-only screen-space one-bounce gather in `resources/shaders/lighting/gtao.comp` (compiled with `GI_PASS`), next to the SSGI horizon term. It runs after the lighting pass on this frame's lit colour, is cleaned up by the bilateral GTAO blur (`gtao_blur.comp` with `NO_BENT`) into `Screen GI`, and `gi_apply.comp` adds `diffuseColor * materialAO * GI` to the HDR image with a depth-aware upsample. There is no temporal accumulation and no previous-frame data.

How it works
- Each half-resolution GTAO pixel takes N golden-angle spiral taps inside a world-space radius.
- A tap contributes the lit color (mip 1 of this frame's scene color pyramid, captured right after the lighting pass) weighted by receiver cosine, emitter cosine and a smooth distance falloff. Lit color already contains emitter albedo times its real irradiance (shadows, local lights, sky occlusion), so the gathered light is tinted by the emitter's albedo and a surface in shadow does not bounce sunlight.
- The SSGI term uses the same lit colour for the GTAO sectors that a sample newly occludes (visibility-bitmask GI).
- Before this change both terms lit every emitter with the unshadowed sun (`albedo * E_sun / pi`), which leaked sunlight into closed rooms, made concave corners glow and smeared coloured blotches across walls.
- Cost is a second half-resolution GTAO-style pass (GI only), two blur passes and a full-resolution apply.

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
- Uses the current frame's lit pyramid, so it also works on the first frame and in portal views (the room behind a portal is spliced into depth and G-buffer, see Portals.md).
- Offscreen taps are skipped; only on-screen surfaces can bleed.
- Settings are clamped on the CPU (`colorBleedUniform` in `Engine.Render.settings`).
