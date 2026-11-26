#pragma once

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
    static void silenceEngineLog(bool silent);
};

}
