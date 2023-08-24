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

Entity *selected_entity;
Light *selected_light;
LitButton *selected_button;
Text *selected_textElement;



typedef struct HitInfo
{
    bool hit;
    Vector3 worldPoint;
    Vector3 relativePoint; // Relative Hit Position from Origin
    Vector3 worldNormal;
    float distance;
    Color hitColor;
    std::shared_ptr<Entity> entity;
};


#endif // ENGINE_H
