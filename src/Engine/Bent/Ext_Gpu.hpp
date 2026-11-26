#pragma once

namespace bent {

void setGpuMode(bool enabled);
[[nodiscard]] bool gpuMode();
[[nodiscard]] bool gpuUsable();

}
