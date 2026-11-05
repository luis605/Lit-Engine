#include "EdgeSuites.hpp"
#include "TestSupport.hpp"

int main() {
    runEdgeGeometryTests();
    runEdgeGizmoTextTests();
    runEdgeEventsSelectionTests();
    runEdgeHistoryProfilerTests();
    return testsupport::finish();
}
