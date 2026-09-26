#pragma once

#include <Display/DisplayTypes.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Display {

// Every message is one command byte and one 35-byte plane in the logical
// board layout. The content command shows the board; the attribute commands
// only stage their plane, which the next content command applies together
// with the content. An old Display Controller drops what it does not know.
constexpr uint8_t kSetDisplayCommand = 0x01U;  // content
constexpr uint8_t kSetBlinkCommand = 0x02U;    // BoardAttributes::blink
constexpr uint8_t kSetLevel0Command = 0x03U;   // BoardAttributes::level0
constexpr uint8_t kSetLevel1Command = 0x04U;   // BoardAttributes::level1

constexpr uint8_t kAttributeMessageCount = 3;
using AttributeMessages = std::array<I2cMessage, kAttributeMessageCount>;

void serializeI2c(const LogicalBoardState& state, I2cMessage& message);
void serializePlaneI2c(uint8_t command, const LogicalBoardState& plane, I2cMessage& message);
// The three attribute messages, in the order to send them before the content.
void serializeAttributesI2c(const BoardAttributes& attributes, AttributeMessages& messages);

// Content only: accepts a whole message with the content command.
bool deserializeI2c(const uint8_t* data, std::size_t size, LogicalBoardState& destination);
// Any known command: reports which and decodes its plane. Anything else,
// including a wrong size, is rejected and leaves both outputs untouched.
bool deserializePlaneI2c(const uint8_t* data, std::size_t size, uint8_t& command,
                         LogicalBoardState& plane);

// The attribute plane a command carries, or nullptr for the content command
// and unknown ones.
LogicalBoardState* attributePlane(BoardAttributes& attributes, uint8_t command);

} // namespace Display
