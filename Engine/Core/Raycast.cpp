/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Raycast.hpp"
#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Engine.hpp>

HitInfo raycast(const LitVector3& origin, const LitVector3& direction, const bool& debug, const std::vector<Entity>& ignore) {
    HitInfo hitInfo;
    hitInfo.hit = false;

    Ray ray{origin, direction};

    if (debug)
        DrawRay(ray, RED);

    if (entitiesList.empty())
        return hitInfo;

    float minDistance = FLT_MAX;

    for (const Entity& entity : entitiesList) {
        if (std::find(ignore.begin(), ignore.end(), entity) != ignore.end())
            continue;

        if (!entity.getFlag(Entity::Flag::COLLIDER))
            continue;

        RayCollision entityBounds = GetRayCollisionBox(ray, entity.bounds);

        for (int mesh_i = 0; mesh_i < entity.model.meshCount && entityBounds.hit; mesh_i++) {
            RayCollision meshHitInfo = GetRayCollisionMesh(ray, entity.model.meshes[mesh_i], entity.model.transform);

            if (meshHitInfo.hit && meshHitInfo.distance < minDistance) {
                minDistance = meshHitInfo.distance;

                hitInfo.hit = true;
                hitInfo.distance = minDistance;
                hitInfo.entity = const_cast<Entity*>(&entity);
                hitInfo.worldPoint = meshHitInfo.point;
                hitInfo.worldNormal = meshHitInfo.normal;
            }
        }
    }

    return hitInfo;
}