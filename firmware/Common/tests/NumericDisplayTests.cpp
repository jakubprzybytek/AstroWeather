#include <Display/DisplayTypes.hpp>

#include <Expect.hpp>

#include <array>
#include <iomanip>
#include <iostream>
#include <limits>

namespace {

using Display::NumericDisplay;
using Display::NumericSegments;

constexpr uint8_t kA = 0x01U;
constexpr uint8_t kB = 0x02U;
constexpr uint8_t kC = 0x04U;
constexpr uint8_t kD = 0x08U;
constexpr uint8_t kE = 0x10U;
constexpr uint8_t kG = 0x40U;
constexpr uint8_t kDp = 0x80U;

constexpr std::array<uint8_t, 10> kDigits = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
};

constexpr std::array<uint8_t, 5> kError = {kD, kD, kD, kD, 0U};

uint8_t withDp(uint8_t segments) { return static_cast<uint8_t>(segments | kDp); }

void printSlots(const std::array<uint8_t, 5> &slots) {
  std::cerr << std::hex << std::setfill('0');
  for (const uint8_t slot : slots) {
    std::cerr << " 0x" << std::setw(2) << static_cast<int>(slot);
  }
  std::cerr << std::dec << std::setfill(' ');
}

void expectSlots(const NumericSegments &actual,
                 const std::array<uint8_t, 5> &expected, const char *caseName) {
  if (actual.slots != expected) {
    std::cerr << caseName << ": expected";
    printSlots(expected);
    std::cerr << ", got";
    printSlots(actual.slots);
    std::cerr << '\n';
    Test::fail(caseName);
  }
}

void testSetFixed() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setFixed(1234);
  expectSlots(data, {kDigits[1], kDigits[2], kDigits[3], kDigits[4], 0U},
              "1234");

  display.setFixed(1234, 1);
  expectSlots(data,
              {kDigits[1], kDigits[2], withDp(kDigits[3]), kDigits[4], 0U},
              "123.4");

  display.setFixed(1234, 2);
  expectSlots(data,
              {kDigits[1], withDp(kDigits[2]), kDigits[3], kDigits[4], 0U},
              "12.34");

  display.setFixed(1234, 3);
  expectSlots(data,
              {withDp(kDigits[1]), kDigits[2], kDigits[3], kDigits[4], 0U},
              "1.234");

  display.setFixed(1004);
  expectSlots(data, {kDigits[1], kDigits[0], kDigits[0], kDigits[4], 0U},
              "inner zeros shown");

  display.setFixed(-999, 1);
  expectSlots(data, {kG, kDigits[9], withDp(kDigits[9]), kDigits[9], 0U},
              "-99.9");

  display.setFixed(-999);
  expectSlots(data, {kG, kDigits[9], kDigits[9], kDigits[9], 0U},
              "negative capacity limit");

  display.setFixed(10000);
  expectSlots(data, kError, "too large");

  display.setFixed(-1000);
  expectSlots(data, kError, "negative overflow");

  display.setFixed(5, 4);
  expectSlots(data, kError, "unsupported precision");
}

// The digit left of the decimal point is always shown, so magnitudes below
// one keep their "0." and the minus sign goes left of that zero.
void testSetFixedBelowOne() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setFixed(5, 1);
  expectSlots(data, {0U, 0U, withDp(kDigits[0]), kDigits[5], 0U}, "0.5");

  display.setFixed(-5, 1);
  expectSlots(data, {0U, kG, withDp(kDigits[0]), kDigits[5], 0U}, "-0.5");

  display.setFixed(-12, 1);
  expectSlots(data, {0U, kG, withDp(kDigits[1]), kDigits[2], 0U}, "-1.2");

  display.setFixed(0, 1);
  expectSlots(data, {0U, 0U, withDp(kDigits[0]), kDigits[0], 0U}, "0.0");

  display.setFixed(5, 2);
  expectSlots(data, {0U, withDp(kDigits[0]), kDigits[0], kDigits[5], 0U},
              "0.05");

  display.setFixed(-1, 2);
  expectSlots(data, {kG, withDp(kDigits[0]), kDigits[0], kDigits[1], 0U},
              "-0.01");

  display.setFixed(-99, 2);
  expectSlots(data, {kG, withDp(kDigits[0]), kDigits[9], kDigits[9], 0U},
              "-0.99");

  display.setFixed(1, 3);
  expectSlots(data, {withDp(kDigits[0]), kDigits[0], kDigits[0], kDigits[1], 0U},
              "0.001");

  display.setFixed(0, 3);
  expectSlots(data, {withDp(kDigits[0]), kDigits[0], kDigits[0], kDigits[0], 0U},
              "0.000");

  // "-0.001" and "-0.123" need five positions; like any other value that does
  // not fit, they give the error pattern.
  display.setFixed(-1, 3);
  expectSlots(data, kError, "-0.001 does not fit");

  display.setFixed(-123, 3);
  expectSlots(data, kError, "-0.123 does not fit");
}

void testSetValueInteger() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setValue(static_cast<int16_t>(0));
  expectSlots(data, {0U, 0U, 0U, kDigits[0], 0U}, "zero");

  display.setValue(static_cast<int16_t>(-1));
  expectSlots(data, {0U, 0U, kG, kDigits[1], 0U}, "negative one");

  display.setValue(static_cast<int16_t>(42));
  expectSlots(data, {0U, 0U, kDigits[4], kDigits[2], 0U},
              "leading zeros blank");

  display.setValue(std::numeric_limits<int16_t>::min());
  expectSlots(data, kError, "negative int16 minimum");

  display.setValue(std::numeric_limits<int16_t>::max());
  expectSlots(data, kError, "int16 maximum");
}

void testSetValueFloat() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setValue(12.345F, 2);
  expectSlots(data,
              {kDigits[1], withDp(kDigits[2]), kDigits[3], kDigits[5], 0U},
              "rounded float");

  display.setValue(0.001F, 3);
  expectSlots(data, {withDp(kDigits[0]), kDigits[0], kDigits[0], kDigits[1], 0U},
              "small positive float");

  display.setValue(std::numeric_limits<float>::quiet_NaN());
  expectSlots(data, kError, "NaN");

  display.setValue(std::numeric_limits<float>::infinity(), 1);
  expectSlots(data, kError, "infinity");

  display.setValue(1.0F, 4);
  expectSlots(data, kError, "float unsupported precision");

  display.setValue(-9.876F, 2);
  expectSlots(data, {kG, withDp(kDigits[9]), kDigits[8], kDigits[8], 0U},
              "rounded negative float");

  display.setValue(-0.01F, 2);
  expectSlots(data, {kG, withDp(kDigits[0]), kDigits[0], kDigits[1], 0U},
              "small negative float");

  display.setValue(0.5F, 1);
  expectSlots(data, {0U, 0U, withDp(kDigits[0]), kDigits[5], 0U},
              "0.5 degrees");

  display.setValue(-0.5F, 1);
  expectSlots(data, {0U, kG, withDp(kDigits[0]), kDigits[5], 0U},
              "-0.5 degrees");

  display.setValue(-2.0F, 1);
  expectSlots(data, {0U, kG, withDp(kDigits[2]), kDigits[0], 0U},
              "-2.0 degrees");

  display.setValue(-99.9F, 1);
  expectSlots(data, {kG, kDigits[9], withDp(kDigits[9]), kDigits[9], 0U},
              "-99.9 degrees");

  display.setValue(-100.0F, 1);
  expectSlots(data, kError, "-100.0 degrees does not fit");

  display.setValue(999.9F, 1);
  expectSlots(data, {kDigits[9], kDigits[9], withDp(kDigits[9]), kDigits[9], 0U},
              "999.9 degrees");

  display.setValue(1000.0F, 1);
  expectSlots(data, kError, "1000.0 degrees does not fit");

  display.setValue(-0.04F, 1);
  expectSlots(data, {0U, 0U, withDp(kDigits[0]), kDigits[0], 0U},
              "-0.04 rounds to 0.0");
}

void testSetTime() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setTime(3, 7);
  expectSlots(data,
              {0U, kDigits[3], kDigits[0], kDigits[7],
               static_cast<uint8_t>(kA | kB)},
              "single-digit hour");

  display.setTime(0, 0);
  expectSlots(data,
              {0U, kDigits[0], kDigits[0], kDigits[0],
               static_cast<uint8_t>(kA | kB)},
              "midnight");

  display.setTime(23, 7);
  expectSlots(data,
              {kDigits[2], kDigits[3], kDigits[0], kDigits[7],
               static_cast<uint8_t>(kA | kB)},
              "two-digit hour");

  display.setTime(100, 0);
  expectSlots(data, kError, "invalid hour");

  display.setTime(0, 100);
  expectSlots(data, kError, "invalid minute");
}

void testSetTimeUnset() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setTimeUnset();
  expectSlots(data, {kG, kG, kG, kG, static_cast<uint8_t>(kA | kB)},
              "unset time");
}

void testSetBlank() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setFixed(1234);
  display.setBlank();
  expectSlots(data, {0U, 0U, 0U, 0U, 0U}, "blank");
}

void testSetSegments() {
  NumericSegments data{};
  NumericDisplay display(data);

  const NumericSegments custom{{kA, kB, kC, kD, kE}};
  display.setSegments(custom);
  expectSlots(data, custom.slots, "raw segments");
}

void testSetNoData() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setTime(12, 34);
  display.setNoData();
  expectSlots(data, {0U, 0U, 0U, kG, 0U}, "no data: G on the last digit only");
}

void testNoDataState() {
  const Display::LogicalBoardState state = Display::noDataState();
  for (const NumericSegments &numeric : state.numeric) {
    expectSlots(numeric, {0U, 0U, 0U, kG, 0U}, "no-data board: every numeric shows the last-digit G");
  }
  for (uint32_t row : state.matrix) {
    Test::expectEqual(row, 0U, "no-data board: matrix blank");
  }
}

} // namespace

int main() {
  testSetFixed();
  testSetFixedBelowOne();
  testSetValueInteger();
  testSetValueFloat();
  testSetTime();
  testSetTimeUnset();
  testSetBlank();
  testSetSegments();
  testSetNoData();
  testNoDataState();
  return Test::finish("NumericDisplay");
}
