#include <Display/DisplayTypes.hpp>

#include <array>
#include <cstdlib>
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
constexpr uint8_t kF = 0x20U;
constexpr uint8_t kG = 0x40U;
constexpr uint8_t kDp = 0x80U;

constexpr std::array<uint8_t, 10> kDigits = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
};

void expectEqual(const NumericSegments &actual,
                 const std::array<uint8_t, 5> &expected, const char *caseName) {
  if (actual.slots != expected) {
    std::cerr << caseName << " failed\n";
    std::exit(EXIT_FAILURE);
  }
}

void testSetFixed() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setFixed(1234);
  expectEqual(data, {kDigits[1], kDigits[2], kDigits[3], kDigits[4], 0U},
              "1234");

  display.setFixed(1234, 2);
  expectEqual(data,
              {kDigits[1], static_cast<uint8_t>(kDigits[2] | kDp), kDigits[3],
               kDigits[4], 0U},
              "12.34");

  display.setFixed(-999, 1);
  expectEqual(
      data,
      {kG, kDigits[9], static_cast<uint8_t>(kDigits[9] | kDp), kDigits[9], 0U},
      "-99.9");

  display.setFixed(-999);
  expectEqual(data, {kG, kDigits[9], kDigits[9], kDigits[9], 0U},
              "negative capacity limit");

  display.setFixed(10000);
  expectEqual(data, {kD, kD, kD, kD, 0U}, "too large");

  display.setFixed(-1000);
  expectEqual(data, {kD, kD, kD, kD, 0U}, "negative overflow");
}

void testSetValueInteger() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setValue(static_cast<int16_t>(0));
  expectEqual(data, {0U, 0U, 0U, kDigits[0], 0U}, "zero");

  display.setValue(static_cast<int16_t>(-1));
  expectEqual(data, {0U, 0U, kG, kDigits[1], 0U}, "negative one");

  display.setValue(std::numeric_limits<int16_t>::min());
  expectEqual(data, {kD, kD, kD, kD, 0U}, "negative int16 minimum");
}

void testSetValueFloat() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setValue(12.345F, 2);
  expectEqual(data,
              {kDigits[1], static_cast<uint8_t>(kDigits[2] | kDp), kDigits[3],
               kDigits[5], 0U},
              "rounded float");

  display.setValue(0.001F, 3);
  expectEqual(data, {kDp, 0U, 0U, kDigits[1], 0U}, "small positive float");

  display.setValue(std::numeric_limits<float>::quiet_NaN());
  expectEqual(data, {kD, kD, kD, kD, 0U}, "NaN");

  display.setValue(-9.876F, 2);
  expectEqual(
      data,
      {kG, static_cast<uint8_t>(kDigits[9] | kDp), kDigits[8], kDigits[8], 0U},
      "rounded negative float");

  display.setValue(-0.01F, 2);
  expectEqual(data, {0U, kDp, kG, kDigits[1], 0U}, "small negative float");
}

void testSetTime() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setTime(3, 7);
  expectEqual(data,
              {kDigits[0], kDigits[3], kDigits[0], kDigits[7],
               static_cast<uint8_t>(kA | kB)},
              "time");

  display.setTime(100, 0);
  expectEqual(data, {kD, kD, kD, kD, 0U}, "invalid time");
}

void testSetBlank() {
  NumericSegments data{};
  NumericDisplay display(data);

  display.setBlank();
  expectEqual(data, {0U, 0U, 0U, 0U, 0U}, "blank");
}

void testSetSegments() {
  NumericSegments data{};
  NumericDisplay display(data);

  const NumericSegments custom{{kA, kB, kC, kD, kE}};
  display.setSegments(custom);
  expectEqual(data, custom.slots, "raw segments");
}

} // namespace

int main() {
  testSetFixed();
  testSetValueInteger();
  testSetValueFloat();
  testSetTime();
  testSetBlank();
  testSetSegments();
  std::cout << "NumericDisplay tests passed\n";
  return EXIT_SUCCESS;
}