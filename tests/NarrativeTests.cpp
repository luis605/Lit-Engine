#include "TestSupport.hpp"
#include "NarrativeSuites.hpp"

int main() {
    runStateTests();
    runStoryletTests();
    runObservationTests();
    runSaveTests();
    runCameraRigTests();
    return testsupport::finish();
}
