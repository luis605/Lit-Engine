#pragma once

// Large-world renderer test scene (docs/LargeWorld.md): content 500 km (configurable) from the world origin, a 4 km flight corridor,
// distant landmarks at 20 / 50 / 100 km and two overlapping walls 60 m apart at 50 km (depth precision probe).
// Shared by LitShot (--testscene largeworld) and tests/LargeWorldRenderTests.cpp.

#include <cstdint>

struct LargeWorldConfig {
    double originKm = 500.0;      // world distance of the scene from the origin along +x
    float flySpeed = 0.0f;        // metres per second along +x (the camera is driven in world coordinates)
    int rebaseFrame = 0;          // > 0: force one rebase (origin moves to the camera, snapped) at that frame, camera held still
    double rebaseThreshold = 400.0;
    double rebaseSnap = 256.0;
    bool autoRebase = true;       // floating_origin system on (threshold / snap above)
};

// The functions below take Engine (a module type), so each user declares them after its imports:
//   void buildLargeWorldScene(Engine&, const LargeWorldConfig&);   builds the scene, enables floating origin, places the camera
//   void stepLargeWorldScene(Engine&, int frame, float dt);        once per frame before render: flight and the scheduled rebase
//   void largeWorldCameraWorld(Engine&, double out[3]);
//   void setLargeWorldFlight(Engine&, float metersPerSecond);      start (or stop with 0) flying along +x from the current camera position
#define LIT_LARGEWORLD_DECLARATIONS                                       \
    void buildLargeWorldScene(Engine& engine, const LargeWorldConfig& config); \
    void stepLargeWorldScene(Engine& engine, int frame, float deltaSeconds);   \
    void largeWorldCameraWorld(Engine& engine, double out[3]);                 \
    void setLargeWorldFlight(Engine& engine, float metersPerSecond);
