#include <Sensors/CurrentSenseConversion.hpp>

#include <Expect.hpp>

#include <cstdint>

using Test::expectEqual;

namespace {

// Example factory values, in the range of real STM32G0 parts: VREFINT_CAL is
// about 1.212 V at 3.0 V (~1655 counts); TS_CAL1/TS_CAL2 are made up but
// ordered like a real sensor (counts rise with temperature).
constexpr uint16_t kVrefIntCal = 1655U;
constexpr uint16_t kTsCal1 = 1040U;  // 30 C at 3.0 V
constexpr uint16_t kTsCal2 = 1380U;  // 130 C at 3.0 V

void testNominalVdda()
{
    expectEqual(CurrentSense::rawToMilliAmps(0U), 0U, "current zero");
    expectEqual(CurrentSense::rawToMilliAmps(255U), 82U,
                "current 255 counts");
    expectEqual(CurrentSense::rawToMilliAmps(511U), 164U,
                "current 511 counts");
    expectEqual(CurrentSense::rawToMilliAmps(4095U), 1320U,
                "current full scale");
}

void testCustomVdda()
{
    expectEqual(CurrentSense::rawToMilliAmps(2048U, 3000U), 600U,
                "current midpoint at 3.0 V");
    // 64-bit intermediate: raw x mV x 1000 overflows 32 bits here.
    expectEqual(CurrentSense::rawToMilliAmps(4095U, 3600U), 1440U,
                "current full scale at 3.6 V");
}

void testVddaFromVrefInt()
{
    // VREFINT_CAL x 3000 / VREFINT_DATA, truncated.
    expectEqual(CurrentSense::vddaMilliVolts(kVrefIntCal, kVrefIntCal), 3000U,
                "vdda at the calibration voltage");
    expectEqual(CurrentSense::vddaMilliVolts(1505U, kVrefIntCal), 3299U,
                "vdda near 3.3 V, truncated");
    expectEqual(CurrentSense::vddaMilliVolts(1379U, kVrefIntCal), 3600U,
                "vdda at 3.6 V");
    expectEqual(CurrentSense::vddaMilliVolts(0U, kVrefIntCal), 3300U,
                "vdda zero reading falls back to 3.3 V");
    expectEqual(CurrentSense::vddaMilliVolts(0U, 0U), 3300U,
                "vdda zero reading without calibration");
}

void testTemperature()
{
    using CurrentSense::temperatureCelsius;
    expectEqual(temperatureCelsius(kTsCal1, 3000U, kTsCal1, kTsCal2), 30,
                "temperature at TS_CAL1");
    expectEqual(temperatureCelsius(kTsCal2, 3000U, kTsCal1, kTsCal2), 130,
                "temperature at TS_CAL2");
    expectEqual(temperatureCelsius(1210U, 3000U, kTsCal1, kTsCal2), 80,
                "temperature midway");
    // (1100 - 1040) x 100 / 340 = 17.6, truncated.
    expectEqual(temperatureCelsius(1100U, 3000U, kTsCal1, kTsCal2), 47,
                "temperature between, truncated");
    // Below TS_CAL1 the division truncates toward zero: 30 - 11.8 -> 19.
    expectEqual(temperatureCelsius(1000U, 3000U, kTsCal1, kTsCal2), 19,
                "temperature below TS_CAL1");
    // The reading is first rescaled to 3.0 V: 946 x 3300 / 3000 = 1040.
    expectEqual(temperatureCelsius(946U, 3300U, kTsCal1, kTsCal2), 30,
                "temperature at TS_CAL1 with VDDA 3.3 V");
    // 1100 x 3300 / 3000 = 1210, midway.
    expectEqual(temperatureCelsius(1100U, 3300U, kTsCal1, kTsCal2), 80,
                "temperature midway with VDDA 3.3 V");
    expectEqual(temperatureCelsius(1100U, 3000U, kTsCal1, kTsCal1),
                CurrentSense::kTemperatureCalcError,
                "temperature with equal calibration points");
}

void testDisplayValue()
{
    using CurrentSense::displayMilliAmps;
    expectEqual(displayMilliAmps(0U), static_cast<int16_t>(0), "display zero");
    expectEqual(displayMilliAmps(1320U), static_cast<int16_t>(1320), "display full scale");
    expectEqual(displayMilliAmps(9999U), static_cast<int16_t>(9999), "display 9999");
    expectEqual(displayMilliAmps(10000U), static_cast<int16_t>(INT16_MAX),
                "display 10000 is the error value");
    // 70000 would wrap to 4464 as a plain int16_t.
    expectEqual(displayMilliAmps(70000U), static_cast<int16_t>(INT16_MAX),
                "display does not wrap");
}

}  // namespace

int main()
{
    testNominalVdda();
    testCustomVdda();
    testVddaFromVrefInt();
    testTemperature();
    testDisplayValue();
    return Test::finish("CurrentSense conversion");
}
