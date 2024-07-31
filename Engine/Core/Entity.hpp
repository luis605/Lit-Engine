#ifndef ENTITY_H
#define ENTITY_H

class Entity;

void removeEntity(int id);
Entity* getEntityById(int id);
int getIdFromEntity(const Entity& entity);

bool Entity_already_registered = false;

enum CollisionShapeType {
    Box           = 0,
    HighPolyMesh  = 1,
    None          = 2
};
#endif // ENTITY_H