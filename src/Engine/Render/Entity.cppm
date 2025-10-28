module;

#include <cstdint>
#include <limits>

export module Engine.Render.entity;

export using Entity = std::uint32_t;
export inline constexpr Entity INVALID_ENTITY = std::numeric_limits<Entity>::max();