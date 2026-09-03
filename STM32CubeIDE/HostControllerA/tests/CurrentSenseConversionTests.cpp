#include <Sensors/CurrentSenseConversion.hpp>

#include <cstdlib>
#include <iostream>

namespace {

void expectEqual(uint32_t actual, uint32_t expected, const char* caseName)
{
    if (actual != expected)
    {
        std::cerr << caseName << " failed: expected " << expected
                  << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testNominalVdda()
{
    expectEqual(CurrentSense::rawToMilliAmps(0U), 0U, "current zero");
    expectEqual(CurrentSense::rawToMilliAmps(255U), 62U,
                "current 255 counts");
    expectEqual(CurrentSense::rawToMilliAmps(511U), 124U,
                "current 511 counts");
    expectEqual(CurrentSense::rawToMilliAmps(4095U), 1000U,
                "current full scale");
}

void testCustomVdda()
{
    expectEqual(CurrentSense::rawToMilliAmps(2048U, 3000U), 600U,
                "current midpoint at 3.0 V");
}

}  // namespace

int main()
{
    testNominalVdda();
    testCustomVdda();
    std::cout << "CurrentSense conversion tests passed\n";
    return EXIT_SUCCESS;
}
