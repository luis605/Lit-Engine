# ADR: Audio engine

Status: accepted

## Context

The engine needs mixing, buses, 3D spatialization, ducking, interruptible voice lines and CPU ray-traced propagation (occlusion, early reflections, reverb estimate, portal hops) without hardware ray tracing. It must run headless and deterministically in tests and CI.

## Decision

- `Engine.Audio` owns the DSP graph in plain CPU code: a bus tree, voices, a Schroeder reverb return, ducking rules and parameter-bound bus gains (`Engine.Params`). `AudioMixer::render` produces interleaved float stereo, so any sink can consume it.
- Sinks implement `AudioBackend` (`open`, `submit`, `close`). Shipped backends are `NullAudioBackend` (counts frames, tracks peak and a hash, used by tests) and `WavAudioBackend` (offline render to 16 bit WAV). `renderAudio` drives a mixer into a backend.
- A real device backend is deliberately not linked yet. The candidate is miniaudio (single header, public domain, Linux/Windows/macOS): add it under `src/vendors/miniaudio`, implement `AudioBackend` over its playback callback calling `AudioMixer::render`, and guard it with a CMake option. Nothing in the mixer depends on the choice, and a commercial middleware would plug in at the same `AudioBackend` seam (or replace the mixer) as the user decides.
- Spatialization is an HRTF-lite model: equal-power interaural level difference, interaural time difference (up to 0.66 ms with fractional delay), head shadow low pass on the far ear and rear low pass. A measured HRTF set can replace `mixVoice` panning later without touching the API.
- `Engine.AudioTrace` (`AudioPropagation`) traces against a `Bvh` built from a `TriangleSoup` (`Engine.Geometry`): occlusion from rays to sample points on a disc around the source, early reflections from a listener ray fan with visibility checks to the source, reverb RT60 from the mean free path and escape fraction (Sabine), and a hop hook (`AudioHopFn`) that lets portals or off-mesh links supply entry and exit points. The apparent arrival direction and path length come from the best hop path when the direct path is occluded.
- A per-frame ray budget (`raysPerFrame`) is enforced across the listener fan, occlusion, hop and reflection rays. Emitters are served nearest first; those that do not fit keep their previous result flagged `stale`.
- `VoiceScheduler` manages spoken lines: priority preemption, non-interruptible lines, resumable lines (rewound by a short amount), pause by distance with hysteresis, an event log and a speech energy readout for lip-sync.

## Consequences

- Everything is deterministic: no wall clock, no hidden randomness (fixed sample patterns, `Pcg32` only for generated test noise).
- Ducking and reverb tails are inherently temporal; ducking reads the trigger bus level of the previous block.
- Without a device backend nothing is audible yet; the Null and WAV backends cover tests and offline captures.
