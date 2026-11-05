#include "Review3Suites.hpp"
#include "TestSupport.hpp"

int main() {
    runReview3AiTests();
    runReview3AudioTests();
    runReview3NarrativeTests();
    runReview3PortalTests();
    runReview3AnimationTests();
    return testsupport::finish();
}
