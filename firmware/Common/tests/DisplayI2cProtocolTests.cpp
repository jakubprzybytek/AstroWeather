#include <Display/DisplayI2cProtocol.hpp>

#include <Expect.hpp>

#include <string>

// Byte layout from docs/Display.md, "I2C Transport": command, numeric 1,
// numeric 2, matrix rows 0-4 (three bytes each, little-endian), numeric 3,
// numeric 4.

namespace {

using Test::expect;
using Test::expectEqual;

Display::LogicalBoardState variedState()
{
    Display::LogicalBoardState state{};
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
            state.numeric[display].slots[slot] = static_cast<uint8_t>(0x10U * display + slot + 1U);
        }
    }
    state.numeric[3].slots[4] = 0xFFU;
    state.matrix = {0x000001U, 0x1FFFFFU, 0x0A5A5AU, 0x100000U, 0x123456U};
    return state;
}

bool sameState(const Display::LogicalBoardState& a, const Display::LogicalBoardState& b)
{
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        if (a.numeric[display].slots != b.numeric[display].slots) {
            return false;
        }
    }
    return a.matrix == b.matrix;
}

void testMessageSize()
{
    expectEqual(Display::kLogicalPayloadSize, 35U, "logical payload is 35 bytes");
    expectEqual(Display::kI2cMessageSize, 36U, "message is 36 bytes");
    expectEqual(sizeof(Display::I2cMessage), 36U, "message type is 36 bytes");
    expectEqual(Display::kSetDisplayCommand, 0x01U, "set display command");
}

void testSerializeLayout()
{
    const Display::LogicalBoardState state = variedState();
    Display::I2cMessage message{};
    message.fill(0xEEU);
    Display::serializeI2c(state, message);

    expectEqual(message[0], 0x01U, "command byte first");
    for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
        expectEqual(message[1U + slot], state.numeric[0].slots[slot], "numeric 1 at offset 1");
        expectEqual(message[6U + slot], state.numeric[1].slots[slot], "numeric 2 at offset 6");
        expectEqual(message[26U + slot], state.numeric[2].slots[slot], "numeric 3 at offset 26");
        expectEqual(message[31U + slot], state.numeric[3].slots[slot], "numeric 4 at offset 31");
    }
    // Row 4 = 0x123456, little-endian at offset 11 + 4 * 3.
    expectEqual(message[23], 0x56U, "row 4 low byte first");
    expectEqual(message[24], 0x34U, "row 4 middle byte");
    expectEqual(message[25], 0x12U, "row 4 high byte");
    // Row 1 = 0x1FFFFF at offset 14.
    expectEqual(message[14], 0xFFU, "row 1 low byte");
    expectEqual(message[16], 0x1FU, "row 1 high byte, bits 21-23 zero");
}

void testSerializeMasksUnusedMatrixBits()
{
    Display::LogicalBoardState state{};
    state.matrix[0] = 0xFFFFFFFFU;
    Display::I2cMessage message{};
    Display::serializeI2c(state, message);
    expectEqual(message[11], 0xFFU, "row 0 byte 0");
    expectEqual(message[12], 0xFFU, "row 0 byte 1");
    expectEqual(message[13], 0x1FU, "row 0 bits 21-23 cleared");
}

void testRoundTrip()
{
    const Display::LogicalBoardState state = variedState();
    Display::I2cMessage message{};
    Display::serializeI2c(state, message);

    Display::LogicalBoardState decoded{};
    expect(Display::deserializeI2c(message.data(), message.size(), decoded), "round trip accepted");
    expect(sameState(decoded, state), "round trip restores the state");
}

void testDeserializeMasksUnusedMatrixBits()
{
    Display::I2cMessage message{};
    Display::serializeI2c(Display::LogicalBoardState{}, message);
    message[13] = 0xFFU; // row 0, bits 16-23
    Display::LogicalBoardState decoded{};
    expect(Display::deserializeI2c(message.data(), message.size(), decoded), "set high bits accepted");
    expectEqual(decoded.matrix[0], 0x1F0000U, "bits 21-23 dropped on receipt");
}

// A rejected message returns false and leaves the destination untouched.
void expectRejected(const uint8_t* data, std::size_t size, const char* caseName)
{
    const Display::LogicalBoardState previous = variedState();
    Display::LogicalBoardState destination = previous;
    expect(!Display::deserializeI2c(data, size, destination), caseName);
    expect(sameState(destination, previous), (std::string(caseName) + " keeps state").c_str());
}

void testRejectedMessages()
{
    Display::I2cMessage message{};
    Display::serializeI2c(Display::LogicalBoardState{}, message);

    expectRejected(message.data(), message.size() - 1U, "short message");
    expectRejected(message.data(), 0U, "empty message");
    expectRejected(nullptr, message.size(), "null data");

    uint8_t longer[Display::kI2cMessageSize + 1U]{};
    longer[0] = Display::kSetDisplayCommand;
    expectRejected(longer, sizeof(longer), "long message");

    for (const uint8_t command : {0x00U, 0x02U, 0xFFU}) {
        message[0] = command;
        expectRejected(message.data(), message.size(), "unknown command");
    }
}

} // namespace

int main()
{
    testMessageSize();
    testSerializeLayout();
    testSerializeMasksUnusedMatrixBits();
    testRoundTrip();
    testDeserializeMasksUnusedMatrixBits();
    testRejectedMessages();
    return Test::finish("DisplayI2cProtocol");
}
