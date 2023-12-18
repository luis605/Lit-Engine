#ifndef ENGINE_H
#define ENGINE_H

#include "../include_all.h"

class Entity;

string colorToString(const Color& color);

typedef struct Light;

variant<Entity*, Light*, Text*, LitButton*> object_in_inspector;

vector<Entity> entities_list_pregame;
vector<Light> lights_list_pregame;
vector<Entity> entities_list;

// std::vector<Cluster> clusters;

Entity *selected_entity = nullptr;
Light *selected_light = nullptr;
LitButton *selected_button = nullptr;
Text *selected_textElement = nullptr;
fs::path selected_material;


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