#include <Display/DisplayI2cProtocol.hpp>

namespace Display {
namespace {

void writeNumeric(const NumericData& value, uint8_t* output)
{
    output[0] = static_cast<uint8_t>(value.mantissa);
    output[1] = static_cast<uint8_t>(static_cast<uint16_t>(value.mantissa) >> 8U);
    output[2] = value.flags;
}

bool validNumeric(NumericData value)
{
    if ((value.flags & 0xE0U) != 0U) {
        return false;
    }
    const NumericMode mode = modeOf(value);
    if (mode == NumericMode::Time || mode == NumericMode::Error) {
        return (value.flags & kPrecisionMask) == 0U &&
               (mode != NumericMode::Error || value.flags == static_cast<uint8_t>(NumericMode::Error));
    }
    if (mode == NumericMode::Blank) {
        return value.flags == static_cast<uint8_t>(NumericMode::Blank);
    }
    return precisionOf(value) <= kMaxPrecision;
}

NumericData readNumeric(const uint8_t* input)
{
    const uint16_t raw = static_cast<uint16_t>(input[0]) |
                         static_cast<uint16_t>(input[1] << 8U);
    return {static_cast<int16_t>(raw), input[2]};
}

} // namespace

void serializeI2c(const LogicalBoardState& state, I2cMessage& message)
{
    message.fill(0U);
    message[0] = kSetDisplayCommand;
    writeNumeric(state.numeric[0], &message[1]);
    writeNumeric(state.numeric[1], &message[4]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint32_t value = state.matrix[row] & kMatrixMask;
        const uint8_t offset = static_cast<uint8_t>(7U + row * 3U);
        message[offset] = static_cast<uint8_t>(value);
        message[offset + 1U] = static_cast<uint8_t>(value >> 8U);
        message[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    }
    writeNumeric(state.numeric[2], &message[22]);
    writeNumeric(state.numeric[3], &message[25]);
}

bool deserializeI2c(const uint8_t* data, std::size_t size, LogicalBoardState& destination)
{
    if (data == nullptr || size != kI2cMessageSize || data[0] != kSetDisplayCommand) {
        return false;
    }
    LogicalBoardState decoded{};
    decoded.numeric[0] = readNumeric(&data[1]);
    decoded.numeric[1] = readNumeric(&data[4]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint8_t offset = static_cast<uint8_t>(7U + row * 3U);
        decoded.matrix[row] = (static_cast<uint32_t>(data[offset]) |
                               (static_cast<uint32_t>(data[offset + 1U]) << 8U) |
                               (static_cast<uint32_t>(data[offset + 2U]) << 16U)) & kMatrixMask;
    }
    decoded.numeric[2] = readNumeric(&data[22]);
    decoded.numeric[3] = readNumeric(&data[25]);
    for (const NumericData& value : decoded.numeric) {
        if (!validNumeric(value)) {
            return false;
        }
    }
    destination = decoded;
    return true;
}

} // namespace Display
