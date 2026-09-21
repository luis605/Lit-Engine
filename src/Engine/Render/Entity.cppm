module;

#include <cstdint>
#include <limits>

export module Engine.Render.entity;

export using Entity = std::uint32_t;
export inline constexpr Entity INVALID_ENTITY = std::numeric_limits<Entity>::max();

export struct EntityHandle {
    Entity index = INVALID_ENTITY;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool isNull() const noexcept { return index == INVALID_ENTITY; }
    constexpr explicit operator bool() const noexcept { return index != INVALID_ENTITY; }
    constexpr bool operator==(const EntityHandle&) const noexcept = default;
};

export inline constexpr EntityHandle NULL_ENTITY{};
