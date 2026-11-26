#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent {

class EngineSim;

[[nodiscard]] const std::vector<ActionSpec>& extWorldActionSpecs();
[[nodiscard]] const std::vector<ObservationSpec>& extWorldObservationSpecs();
bool extWorldApply(EngineSim& sim, const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick);
bool extWorldObserve(EngineSim& sim, const std::string& name, const ValueList& args, const SrcLoc& loc, Value& out);
void extWorldRecordTextSnapshot(EngineSim& sim, int64_t slot);
bool extWorldAliveAtTextSnapshot(EngineSim& sim, int64_t slot, uint32_t index, uint32_t generation);
void extWorldBuiltins(Program& program);

}
