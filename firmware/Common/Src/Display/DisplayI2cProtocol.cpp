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

// The 35-byte plane after the command byte: numeric 1, numeric 2, matrix
// rows 0-4 (three bytes each, little-endian), numeric 3, numeric 4.
void writePayload(const LogicalBoardState& state, uint8_t* payload)
{
    writeNumeric(state.numeric[0], &payload[0]);
    writeNumeric(state.numeric[1], &payload[5]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint32_t value = state.matrix[row] & kMatrixMask;
        const uint8_t offset = static_cast<uint8_t>(10U + row * 3U);
        payload[offset] = static_cast<uint8_t>(value);
        payload[offset + 1U] = static_cast<uint8_t>(value >> 8U);
        payload[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    }
    writeNumeric(state.numeric[2], &payload[25]);
    writeNumeric(state.numeric[3], &payload[30]);
}

LogicalBoardState readPayload(const uint8_t* payload)
{
    LogicalBoardState decoded{};
    decoded.numeric[0] = readNumeric(&payload[0]);
    decoded.numeric[1] = readNumeric(&payload[5]);
    for (uint8_t row = 0; row < kMatrixRowCount; ++row) {
        const uint8_t offset = static_cast<uint8_t>(10U + row * 3U);
        decoded.matrix[row] = (static_cast<uint32_t>(payload[offset]) |
                               (static_cast<uint32_t>(payload[offset + 1U]) << 8U) |
                               (static_cast<uint32_t>(payload[offset + 2U]) << 16U)) & kMatrixMask;
    }
    decoded.numeric[2] = readNumeric(&payload[25]);
    decoded.numeric[3] = readNumeric(&payload[30]);
    return decoded;
}

bool knownCommand(uint8_t command)
{
    return command == kSetDisplayCommand || command == kSetBlinkCommand ||
           command == kSetLevel0Command || command == kSetLevel1Command;
}

} // namespace

void serializeI2c(const LogicalBoardState& state, I2cMessage& message)
{
    serializePlaneI2c(kSetDisplayCommand, state, message);
}

void serializePlaneI2c(uint8_t command, const LogicalBoardState& plane, I2cMessage& message)
{
    message.fill(0U);
    message[0] = command;
    writePayload(plane, &message[1]);
}

void serializeAttributesI2c(const BoardAttributes& attributes, AttributeMessages& messages)
{
    serializePlaneI2c(kSetBlinkCommand, attributes.blink, messages[0]);
    serializePlaneI2c(kSetLevel0Command, attributes.level0, messages[1]);
    serializePlaneI2c(kSetLevel1Command, attributes.level1, messages[2]);
}

bool deserializeI2c(const uint8_t* data, std::size_t size, LogicalBoardState& destination)
{
    if (data == nullptr || size != kI2cMessageSize || data[0] != kSetDisplayCommand) {
        return false;
    }
    destination = readPayload(&data[1]);
    return true;
}

bool deserializePlaneI2c(const uint8_t* data, std::size_t size, uint8_t& command,
                         LogicalBoardState& plane)
{
    if (data == nullptr || size != kI2cMessageSize || !knownCommand(data[0])) {
        return false;
    }
    command = data[0];
    plane = readPayload(&data[1]);
    return true;
}

LogicalBoardState* attributePlane(BoardAttributes& attributes, uint8_t command)
{
    switch (command) {
    case kSetBlinkCommand:
        return &attributes.blink;
    case kSetLevel0Command:
        return &attributes.level0;
    case kSetLevel1Command:
        return &attributes.level1;
    default:
        return nullptr;
    }
}

void serializeSync(uint32_t frame, uint16_t micros, SyncMessage& message)
{
    message[0] = kSyncCommand;
    for (uint8_t index = 0; index < 4U; ++index) {
        message[1U + index] = static_cast<uint8_t>(frame >> (8U * index));
    }
    message[5] = static_cast<uint8_t>(micros);
    message[6] = static_cast<uint8_t>(micros >> 8U);
}

bool deserializeSync(const uint8_t* data, std::size_t size, uint32_t& frame, uint16_t& micros)
{
    if (data == nullptr || size != kSyncMessageSize || data[0] != kSyncCommand) {
        return false;
    }
    uint32_t decodedFrame = 0U;
    for (uint8_t index = 0; index < 4U; ++index) {
        decodedFrame |= static_cast<uint32_t>(data[1U + index]) << (8U * index);
    }
    frame = decodedFrame;
    micros = static_cast<uint16_t>(data[5] | (static_cast<uint16_t>(data[6]) << 8U));
    return true;
}

} // namespace Display
