#pragma once

// World / core extension of the Bent engine bridge (laws in tests/bent/laws/ext_world_*.bent, docs/Bent.md "World and core laws").
// Plain header: the hooks are called from EngineHost.cpp / EngineObserve.cpp. Everything here has a unique name (prefixes are
// documented in docs/Bent.md) so other extensions never collide.

#include <functional>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

namespace bent {

class EngineSim;

[[nodiscard]] const std::vector<ActionSpec>& extWorldActionSpecs();
[[nodiscard]] const std::vector<ObservationSpec>& extWorldObservationSpecs();
// Returns true when `action` belongs to this extension (and was applied).
bool extWorldApply(EngineSim& sim, const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick);
// Returns true when `name` is one of this extension's observations; the result is stored in `out`.
bool extWorldObserve(EngineSim& sim, const std::string& name, const ValueList& args, const SrcLoc& loc, Value& out);
// Text snapshots remember which handles were alive when taken: restoreText maps a reference only if its entity was one of them
// (a slot reused after the snapshot must not alias the entity the snapshot brings back).
void extWorldRecordTextSnapshot(EngineSim& sim, int64_t slot);
bool extWorldAliveAtTextSnapshot(EngineSim& sim, int64_t slot, uint32_t index, uint32_t generation);
// Defines the extension's actions, observations and world-free natives (rng*, textTable*, jobs*, param*, replay*).
void extWorldBuiltins(Program& program);

}
