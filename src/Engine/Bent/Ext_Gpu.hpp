#pragma once

// GPU mode of Bent (docs/Bent.md, "GPU mode"): laws in tests/bent/laws/gpu/*.bent render real frames through LitShot.
// Plain header (no module imports) so the CLI can include it.

namespace bent {

// `bent ... --gpu`: enables the rxGpu* natives. Without it they raise an error and the CLI skips laws under a gpu/ directory.
void setGpuMode(bool enabled);
[[nodiscard]] bool gpuMode();
// Renders one tiny frame to find out whether LitShot exists and a device works (cached). False means "skip with exit code 77".
[[nodiscard]] bool gpuUsable();

}
