# GPU pass timings (measured)

Machine: NVIDIA GeForce GTX 1650 (4 GB), driver 595.91.07, Vulkan 1.4.329, Wayland session (window came up 1900x1008 although 1280x720 was requested, the compositor resized it). Captured with `LitShot --frames 40 --timings`, values are `Engine::gpuTimings()` of the last frame in milliseconds, single run, desktop compositor active and other builds running, so treat as +-10 percent. Regenerate with `scripts/render_check.sh` (it writes `<scene>_<variant>.timings.txt`).

Variants: baseline = default settings (reflectionMode SSRProbes, bleed, contact shadows, bent normals, area lights all on), refl0 = reflectionMode 0 (Off), refl4 = World space, scale05 = renderScale 0.5 with EASU/RCAS.

## Frame totals (sum of passes, ms)

| Scene | baseline | refl0 | refl1 SSR | refl2 SSR+Probes | refl3 Planar | refl4 World | scale05 | bleed off | bent off |
|---|---|---|---|---|---|---|---|---|---|
| showcase | 19.13 | 16.01 | 19.34 | 19.75 | 19.73 | 20.46 | 7.40 | 17.40 | 19.08 |
| vista | 13.06 | 12.06 | 12.62 | 13.19 | 12.64 | 12.74 | 5.97 | 13.00 | 13.01 |
| crowd | 13.39 | 10.43 | 13.22 | 13.47 | 13.32 | 13.95 | 6.05 | 12.99 | 12.70 |
| portal_torture | 17.60 | 16.24 | 17.56 | 17.55 | 17.62 | 18.09 | 7.00 | 15.44 | 17.38 |

## Per pass, showcase

| Pass | baseline | refl0 | refl4 | scale05 |
|---|---|---|---|---|
| Sky+IBL | 1.76 | 1.80 | 1.77 | 1.77 |
| Shadows | 0.93 | 1.04 | 0.96 | 0.94 |
| Cull | 0.02 | 0.02 | 0.02 | 0.02 |
| CmdGen | 0.02 | 0.02 | 0.02 | 0.02 |
| GBuffer | 0.38 | 0.39 | 0.91 | 0.12 |
| Hi-Z | 0.21 | 0.21 | 0.21 | 0.07 |
| Retest Cull | 0.01 | 0.01 | 0.02 | 0.01 |
| Retest CmdGen | 0.01 | 0.01 | 0.03 | 0.01 |
| Retest GBuffer | 0.01 | 0.01 | 0.02 | 0.01 |
| Trans Cull | 0.01 | 0.01 | 0.01 | 0.01 |
| Trans CmdGen | 0.02 | 0.02 | 0.02 | 0.02 |
| GTAO | 3.55 | 3.68 | 3.58 | 0.52 |
| SSR | 6.96 | 6.40 | 6.51 | 1.64 |
| Lighting | 3.36 | 0.00 | 3.96 | 0.84 |
| Trans Draw | 0.44 | 0.44 | 0.44 | 0.13 |
| Post | 1.44 | 1.96 | 1.99 | 1.27 |
| Total | 19.13 | 16.01 | 20.46 | 7.40 |

## Per pass, crowd

| Pass | baseline | refl0 | refl4 | scale05 |
|---|---|---|---|---|
| Upload | 0.02 | 0.02 | 0.02 | 0.02 |
| Transform | 0.01 | 0.01 | 0.01 | 0.01 |
| Sky+IBL | 1.77 | 1.79 | 2.37 | 1.75 |
| Shadows | 0.63 | 0.51 | 0.56 | 0.50 |
| Cull | 0.02 | 0.02 | 0.04 | 0.02 |
| CmdGen | 0.02 | 0.02 | 0.03 | 0.02 |
| GBuffer | 0.45 | 0.45 | 0.46 | 0.12 |
| Hi-Z | 0.21 | 0.21 | 0.21 | 0.07 |
| Retest Cull | 0.01 | 0.02 | 0.03 | 0.01 |
| Retest CmdGen | 0.01 | 0.01 | 0.02 | 0.01 |
| Retest GBuffer | 0.01 | 0.01 | 0.01 | 0.01 |
| Trans Cull | 0.01 | 0.01 | 0.02 | 0.01 |
| Trans CmdGen | 0.02 | 0.02 | 0.03 | 0.02 |
| GTAO | 2.00 | 2.01 | 2.00 | 0.50 |
| SSR | 3.83 | 3.31 | 3.41 | 0.85 |
| Lighting | 2.33 | 0.00 | 2.73 | 0.58 |
| Trans Draw | 0.44 | 0.44 | 0.43 | 0.13 |
| Post | 1.57 | 1.58 | 1.56 | 1.41 |
| Total | 13.39 | 10.43 | 13.95 | 6.05 |

## Observations

- renderScale 0.5 cuts the frame from 13-19 ms to 6-7.4 ms; the fixed floor is Sky+IBL (1.8 ms) and Post (1.3 ms, includes EASU, RCAS, FXAA at window size).
- The pass labelled `SSR` costs 3.8-7 ms in every scene and does not shrink with reflectionMode 0 (Off): it is the largest pass of the frame. With reflectionMode 0 the `Lighting` pass reads 0.00 ms and `SSR` stays at 5.6-6.6 ms, so either the timestamp scopes are attributed to the wrong passes when reflections are off or the pass still runs the GI/bleed work. This needs a look by whoever owns the pass labels (RendererProfiling / Lighting stages).
- GTAO (with denoise and bent normals) is 1.0 ms in the sparse vista, 2.0 ms in crowd and 3.5-3.9 ms in showcase and portal_torture at 1900x1008: it scales with covered pixels.
- World-space reflections (refl4) add 0.5-1 ms (extra GBuffer work and Lighting) over SSR+Probes. Planar adds about 0.5 ms Sky+IBL/Post in showcase.
- Shadows 0.5-0.9 ms, GBuffer/Hi-Z/cull together under 1 ms for these small scenes.

## W8-1 correction: SSR label was mis-attributed

- Root cause of "SSR 5.6-6.6 ms with reflectionMode 0, Lighting 0.00 ms": in `Renderer::shadeScene` (RendererFrame.cpp) the stage table paired `GpuPass::Reflections` with stage 1 and `GpuPass::Lighting` with stage 2, but `Lighting::shade` runs clouds + the lighting compute at stage 1 and the reflections (Hi-Z, SSR, scene colour pyramid, composite) at stage 2. The pass labelled `SSR` was therefore the real Lighting pass, and the pass labelled `Lighting` was the reflections work, which is skipped when reflectionMode is 0 (so it read 0.00 ms). Reflections already cost nothing when disabled; the table above shows the lighting cost under the wrong name. Fixed by swapping the labels in the stage table (stage order unchanged).
- Tables above keep their original (wrong) labels: read column `SSR` as Lighting and `Lighting` as Reflections for those measurements. Not re-measured: the tree rendered black frames after 08:30 on 2026-11-04 (cause outside the reflection files, in progress by another agent), so no new timings were possible.

## W8-1 SSR changes (not GPU-verified, see log)

- SSR trace now runs at half resolution (SSR texture is ceil(w/2) x ceil(h/2), trace uses the full-resolution depth/normal at pixel*2+1); reflection_composite.comp upsamples with a depth-aware bilinear filter (4 taps, weights = bilinear x exp(-relative linear depth difference x 400), alpha-weighted colour). Expected: SSR trace cost about 1/4.
- Image diffs against the previous output were not possible (black frames), so the "no visible loss" claim is untested.
