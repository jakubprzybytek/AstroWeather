// Board address straps, read through the stub GPIO model. docs/Display.md
// ("I2C Transport"): a pin tied to VCC is state 2, a floating pin state 1 and
// a pin tied to ground state 0; board_id = ADDR_0 + 3 * ADDR_1 + 9 * ADDR_2
// and the address is 0x10 + board_id.

#include <Display/DisplayAddress.hpp>

#include <Expect.hpp>
#include <StubHal.hpp>

#include <string>

namespace {

using Stub::Strap;
using Test::expect;
using Test::expectEqual;

constexpr Strap kStraps[3] = {Strap::Low, Strap::Floating, Strap::High};

struct Pin {
    GPIO_TypeDef* port;
    uint16_t pin;
};

const Pin kAddressPins[3] = {
    {ADDR_0_GPIO_Port, ADDR_0_Pin},
    {ADDR_1_GPIO_Port, ADDR_1_Pin},
    {ADDR_2_GPIO_Port, ADDR_2_Pin},
};

void strap(uint8_t state0, uint8_t state1, uint8_t state2)
{
    Stub::reset();
    Stub::setStrap(kAddressPins[0].port, kAddressPins[0].pin, kStraps[state0]);
    Stub::setStrap(kAddressPins[1].port, kAddressPins[1].pin, kStraps[state1]);
    Stub::setStrap(kAddressPins[2].port, kAddressPins[2].pin, kStraps[state2]);
}

void expectPinsWithoutPull(const std::string& caseName)
{
    for (const Pin& pin : kAddressPins) {
        expectEqual(Stub::configuredPull(pin.port, pin.pin), static_cast<uint32_t>(GPIO_NOPULL),
                    (caseName + " leaves the pin without pull").c_str());
    }
}

void testAllStrapCombinations()
{
    bool seen[27] = {};
    for (uint8_t state2 = 0; state2 < 3U; ++state2) {
        for (uint8_t state1 = 0; state1 < 3U; ++state1) {
            for (uint8_t state0 = 0; state0 < 3U; ++state0) {
                const std::string caseName = "straps " + std::to_string(state0) + "/" +
                                             std::to_string(state1) + "/" + std::to_string(state2);
                strap(state0, state1, state2);
                const uint8_t id = Display::detectBoardId(
                    ADDR_0_GPIO_Port, ADDR_0_Pin, ADDR_1_GPIO_Port, ADDR_1_Pin,
                    ADDR_2_GPIO_Port, ADDR_2_Pin);
                const unsigned expected = state0 + 3U * state1 + 9U * state2;
                expectEqual(id, expected, caseName.c_str());
                if (id < 27U) {
                    seen[id] = true;
                }
                expectPinsWithoutPull(caseName);

                strap(state0, state1, state2);
                expectEqual(Display::detectBoardAddress(), static_cast<uint16_t>(0x10U + expected),
                            (caseName + " address").c_str());
                expectPinsWithoutPull(caseName + " address");
            }
        }
    }
    for (bool found : seen) {
        expect(found, "every board id reachable");
    }
}

void testDocumentedExamples()
{
    strap(0, 0, 0);
    expectEqual(Display::detectBoardAddress(), 0x10U, "all grounded is 0x10");
    strap(1, 1, 1);
    expectEqual(Display::detectBoardAddress(), 0x1DU, "all floating is 0x1D");
    strap(2, 2, 2);
    expectEqual(Display::detectBoardAddress(), 0x2AU, "all high is 0x2A");
    strap(2, 0, 0);
    expectEqual(Display::detectBoardAddress(), 0x12U, "ADDR_0 is the least significant digit");
    strap(0, 0, 1);
    expectEqual(Display::detectBoardAddress(), 0x19U, "ADDR_2 is the most significant digit");
}

void testBoardAddressRange()
{
    expectEqual(Display::boardAddress(0U), 0x10U, "board 0");
    expectEqual(Display::boardAddress(13U), 0x1DU, "board 13");
    expectEqual(Display::boardAddress(26U), 0x2AU, "board 26");
    expectEqual(Display::boardAddress(27U), 0U, "board 27 has no address");
    expectEqual(Display::boardAddress(255U), 0U, "board 255 has no address");
}

// The pins are passed in, so any port and pin can be read.
void testOtherPins()
{
    Stub::reset();
    Stub::setStrap(GPIOA, GPIO_PIN_0, Strap::High);
    Stub::setStrap(GPIOA, GPIO_PIN_1, Strap::Floating);
    Stub::setStrap(GPIOC, GPIO_PIN_15, Strap::Low);
    expectEqual(Display::detectBoardId(GPIOA, GPIO_PIN_0, GPIOA, GPIO_PIN_1, GPIOC, GPIO_PIN_15),
                5U, "pins given by the caller");
}

} // namespace

int main()
{
    testAllStrapCombinations();
    testDocumentedExamples();
    testBoardAddressRange();
    testOtherPins();
    return Test::finish("DisplayAddress");
}
