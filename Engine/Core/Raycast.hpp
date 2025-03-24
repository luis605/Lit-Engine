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

HitInfo raycast(LitVector3 origin, LitVector3 direction, bool debug, std::vector<Entity> ignore);

#endif // RAYCAST_HPP