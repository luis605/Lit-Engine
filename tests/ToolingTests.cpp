#include "TestSupport.hpp"
#include "ToolingSuites.hpp"

int main() {
    runRngJsonTests();
    runReplayTests();
    runDumpParamsTests();
    runScenesTests();
    return testsupport::finish();
}
