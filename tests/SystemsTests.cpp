#include "TestSupport.hpp"
#include "SystemsSuites.hpp"

int main() {
    runAiTests();
    runNavGeometryTests();
    runRigFaceTests();
    runAudioTests();
    return testsupport::finish();
}
