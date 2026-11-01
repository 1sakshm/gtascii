#include "TestHarness.h"

int main()
{
    int failedTests = 0;
    for (const auto& test : avtest::registry()) {
        const int before = avtest::failureCount();
        std::printf("[ RUN  ] %s\n", test.name);
        test.fn();
        const bool ok = avtest::failureCount() == before;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", test.name);
        if (!ok) {
            ++failedTests;
        }
    }
    std::printf("\n%zu tests, %d failed, %d failed checks\n", avtest::registry().size(), failedTests,
                avtest::failureCount());
    return failedTests == 0 ? 0 : 1;
}
