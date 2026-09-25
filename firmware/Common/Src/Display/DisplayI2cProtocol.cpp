#include <Display/DisplayI2cProtocol.hpp>

namespace Display {
namespace {

void writeNumeric(const NumericSegments& value, uint8_t* output)
{
    for (uint8_t slot = 0; slot < kBytesPerNumericDisplay; ++slot) {
        output[slot] = value.slots[slot];
    }
}

NumericSegments readNumeric(const uint8_t* input)
{
    NumericSegments value{};
    for (uint8_t slot = 0; slot < kBytesPerNumericDisplay; ++slot) {
        value.slots[slot] = input[slot];
    }
    return value;
}

} // namespace

void serializeI2c(const LogicalBoardState& state, I2cMessage& message)
{
    message.fill(0U);
    message[0] = kSetDisplayCommand;
    writeNumeric(state.numeric[0], &message[1]);
    writeNumeric(state.numeric[1], &message[6]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint32_t value = state.matrix[row] & kMatrixMask;
        const uint8_t offset = static_cast<uint8_t>(11U + row * 3U);
        message[offset] = static_cast<uint8_t>(value);
        message[offset + 1U] = static_cast<uint8_t>(value >> 8U);
        message[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    }
    writeNumeric(state.numeric[2], &message[26]);
    writeNumeric(state.numeric[3], &message[31]);
}

bool deserializeI2c(const uint8_t* data, std::size_t size, LogicalBoardState& destination)
{
    if (data == nullptr || size != kI2cMessageSize || data[0] != kSetDisplayCommand) {
        return false;
    }
    LogicalBoardState decoded{};
    decoded.numeric[0] = readNumeric(&data[1]);
    decoded.numeric[1] = readNumeric(&data[6]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint8_t offset = static_cast<uint8_t>(11U + row * 3U);
        decoded.matrix[row] = (static_cast<uint32_t>(data[offset]) |
                               (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
                               (static_cast<uint32_t>(data[offset + 2U]) << 16U)) & kMatrixMask;
    }
    decoded.numeric[2] = readNumeric(&data[26]);
    decoded.numeric[3] = readNumeric(&data[31]);
    destination = decoded;
    return true;
}

} // namespace Display
