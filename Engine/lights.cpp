#include <string>
#include <iostream>
#include <vector>
#include "raylib.h"

using namespace std;


#define DIRECTIONAL_LIGHT 0
#define POINT_LIGHT       1

typedef struct
{
    int type;
    string name;
    Vector3 position;
    Vector3 target;
    bool enabled;
    bool castShadows;
    float intensity;
    float attenuation;
    Color color;

    // Shader locations
    int typeLoc;
    int enabledLoc;
    int castShadowsLoc;
    int positionLoc;
    int targetLoc;
    int colorLoc;
    int intensityLoc;
    int attenuationLoc;

} Light;

vector<Light> lights;

Light CreateLight(int type, Vector3 position, Shader shader, Vector3 target = Vector3Zero(), Color color = RED)
{
    cout << "Creating light" << endl;
    Light light;
    light.type = type;
    light.position = position;
    light.target = target;
    light.color = color;

    lights.push_back(light);


    return light;
}