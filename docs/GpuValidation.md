# GPU validation of the renderer

The renderer was executed for real on this machine (W5-3). Nothing is missing.

## What exists here

- Real GPU: NVIDIA GeForce GTX 1650, proprietary driver 595.91.07, Vulkan 1.4.329 (ICD `/usr/share/vulkan/icd.d/nvidia_icd.json`), OpenGL 4.6 and EGL via NVIDIA, Wayland plus X (`:0`) session.
- Also installed but unused: Mesa lavapipe (`lvp_icd.json`), Intel/AMD/nouveau ICDs, `VK_LAYER_KHRONOS_validation`.
- Not installed: `vulkaninfo`, `Xvfb`/`xvfb-run`. `glxinfo` and `eglinfo` exist and report the NVIDIA GPU.
- The renderer is Vulkan only (`GetEngineFactoryVk` in `src/Engine/Render/RendererInit.cpp`, `Selected GPU` log line in `RendererGpuHelpers.cpp`). It needs a GLFW window, so a display (the live session, or Xvfb with lavapipe on a machine that has them) is required; there is no offscreen swapchain.

## Tools

- `LitShot` (`src/Shot/main.cpp`, target `LitShot`): opens the engine, builds a test scene, applies `--set key=value` render settings, renders N frames, saves the final back buffer as PNG through `Engine::requestScreenshot` (staging copy, `stb_image_write`) and optionally writes `Engine::gpuTimings()`.
  `LitShot --testscene showcase|vista|crowd|portal_torture|streaming_route --frames 40 --screenshot out.png --timings out.txt --set reflectionMode=4 --set renderScale=0.5`
  `showcase` is a small Cornell-like room built in LitShot: red and green walls (colour bleed), glossy floor, five spheres from mirror to rough (reflections), a tube and a sphere area light (photometric lumens, default 3e6 each), a pillar and block (contact shadows, AO).
- `scripts/render_check.sh [outdir]`: runs every scene with baseline, reflection modes 0-4, bleed off, contact shadows off, bent normals off, area lights off, aoDenoise 0, renderScale 0.5 (with and without sharpening) and the governor. Prints one line per run (exit code, count of Diligent `Error:`/`Warning:` log lines, PNG present), writes PNG, log and timings per run. Env: `SCENES`, `FRAMES`, `BUILD_DIR`, `LITSHOT` (use a copy of the binary when other builds relink it).
- To get validation messages run with `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` (the layer is installed; the run printed no messages, but Diligent does not install a debug messenger in release builds so absence of output is weak evidence; build Diligent in Debug for guaranteed reporting).

## Results of the first run (56 runs: 4 scenes x 14 variants)

- All runs exit 0, all PNGs produced, zero Diligent errors or warnings in any log, no shader compile failures (all new compute and fragment passes link: EASU, RCAS, depth_upscale, bleed, GTAO denoise, bent normals, planar, probe bake, reflection composite).
- No black or NaN frames, no white clipping (black fraction 0 to 0.3 percent, which is the dark floor edge in showcase).
- Every feature switch changes the image in the expected region: reflection off removes sphere/floor reflections, world-space mode adds box-traced reflections, bent normals off darkens/flattens ambient, bleed off removes red/green wash, renderScale 0.5 renders at half size and resolves to the window with the upscaler, depth_upscale keeps overlay depth.
- Runs are deterministic for fixed settings (three repeat runs differ by < 0.02 mean 8-bit level; the governor and the animated portal_torture scene depend on wall-clock dt).

## Findings (not fixed, owners listed)

1. Light units are photometric: `PackLight` divides intensity by 4 pi to candela and exposure matches a 100 klx sun, so a point light needs roughly 1e6 to 1e7 for a visible effect in daylight; the default `LightComponent::intensity` of 1 is invisible next to the sun. Editor and docs should state units (docs/Lighting.md).
2. World-space reflection mode (refl4) shows a hard rectangular edge in the glossy floor reflection and stair-stepping on the wall edge: the traced box proxies end abruptly (see `<outdir>/showcase_refl4.png`, lower right). Needs edge fade or proxy padding in `resources/shaders/lighting/reflection_composite.comp` / tracing code.
3. Pass timing labels: with reflectionMode 0 the `Lighting` pass reports 0.00 ms and `SSR` still 5.6-6.6 ms (docs/GpuTimings.md). Probably the timestamp scope assignment around the reflection stage; the `SSR` pass is the most expensive pass of the frame (3.8-7 ms at 1900x1008 on a GTX 1650).
4. Indoor scenes look veiled: sky IBL and haze reach enclosed rooms because there is no sky visibility term, so lights barely compete. Expected for a no-GI-probe design, worth a sky-occlusion or interior volume option.
5. The window opens at the compositor size (1900x1008 under this Wayland session), not at the requested size.

## Running on another machine

Needs a Vulkan 1.3 device and a display. Headless servers: `xvfb-run -a scripts/render_check.sh` with Mesa lavapipe (`VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`) works if the engine's required device features are exposed by lavapipe (not tried here: Xvfb is not installed on this machine and installing packages is not allowed).
