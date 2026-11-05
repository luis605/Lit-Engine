#pragma once

namespace delta_codec {
inline uint64_t safeSerial(double value) { return value >= 0.0 && value <= 9.0e15 ? static_cast<uint64_t>(value) : 0; }
Json stateJson(const EntityState& state);
EntityState stateFrom(const Json& json);
Json prefabJson(const Prefab& prefab);
Prefab prefabFrom(const Json& nodes);
Json paramsJson(const PortalCellParams& params);
bool sameParams(const PortalCellParams& left, const PortalCellParams& right);
void paramsFrom(const Json& json, PortalCellParams& params);
}
