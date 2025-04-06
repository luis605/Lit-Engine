#ifndef RAYCAST_HPP
#define RAYCAST_HPP

class Entity;

#include <Engine/Scripting/math.hpp>
#include <vector>
#include <raylib.h>

struct HitInfo {
    bool hit;
    LitVector3 worldPoint;
    LitVector3 relativePoint; // Relative Hit Position from Origin of the ray
    LitVector3 worldNormal;
    float distance;
    Color hitColor;
    Entity* entity;
};

HitInfo raycast(const LitVector3& origin, const LitVector3& direction, const bool& debug, const std::vector<Entity>& ignore);

#endif // RAYCAST_HPP