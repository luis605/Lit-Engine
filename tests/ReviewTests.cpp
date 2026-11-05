#include "ReviewSuites.hpp"
#include "TestSupport.hpp"

int main() {
    runReviewJsonTests();
    runReviewWorldTests();
    runReviewIkTests();
    runReviewToolsTests();
    return testsupport::finish();
}
