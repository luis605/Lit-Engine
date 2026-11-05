#include "Review2Suites.hpp"
#include "TestSupport.hpp"

int main() {
    runReview2NavTests();
    runReview2CrowdTests();
    runReview2GeometryTests();
    runReview2AssetTests();
    runReview2SaveTests();
    runReview2StreamingTests();
    return testsupport::finish();
}
