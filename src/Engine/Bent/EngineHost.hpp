#pragma once

// Bent host that interprets scene programs against a fresh headless Engine per after/always/eventually block.
// Plain header (no module imports) so the CLI and tests can include it.

#include <memory>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent {

class EngineHost : public Host {
  public:
    EngineHost();
    ~EngineHost() override;
    [[nodiscard]] std::unique_ptr<Sim> createSim() override;
    [[nodiscard]] const std::vector<ActionSpec>& actions() const override;
    [[nodiscard]] const std::vector<ObservationSpec>& observations() const override;
    void addBuiltins(Program& program) override;
    // Engine log output goes to stdout; the CLI silences it unless --verbose.
    static void silenceEngineLog(bool silent);
};

}
