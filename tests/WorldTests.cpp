#include "WorldSuites.hpp"
#include "TestSupport.hpp"

int main() {
    runEntityTests();
    runSpatialTests();
    runSpatialFarTests();
    runSerializationTests();
    runRobustnessTests();
    runRuntimeTests();
    runPhysicsAnimationTests();
    runEditorToolsTests();
    return testsupport::finish();
}
