#ifndef ENGINE_H
#define ENGINE_H

#include "../include_all.h"

class Entity;

string colorToString(const Color& color);

typedef struct Light;

variant<Entity*, Light*, Text*, LitButton*> objectInInspector;

vector<Entity> entitiesListPregame;
vector<Light> lightsListPregame;
vector<Entity> entitiesList;

// std::vector<Cluster> clusters;

Entity *selectedEntity = nullptr;
Light *selectedLight = nullptr;
LitButton *selectedButton = nullptr;
Text *selectedTextElement = nullptr;
fs::path selectedMaterial;


typedef struct HitInfo
{
    bool hit;
    LitVector3 worldPoint;
    LitVector3 relativePoint; // Relative Hit Position from Origin
    LitVector3 worldNormal;
    float distance;
    Color hitColor;
    Entity* entity;
};


#endif // ENGINE_H
