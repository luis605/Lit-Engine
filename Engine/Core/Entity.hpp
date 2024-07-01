#ifndef ENTITY_H
#define ENTITY_H

void removeEntity(int id);
Entity* getEntityById(int id);
int getIdFromEntity(const Entity& entity);

bool Entity_already_registered = false;

#endif // ENTITY_H