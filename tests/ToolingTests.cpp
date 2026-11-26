#include "TestSupport.hpp"
#include "ToolingSuites.hpp"

int main() {
    runRngJsonTests();
    runReplayTests();
    runDumpParamsTests();
    runScenesTests();
    runPacerTests();
    return testsupport::finish();
}
