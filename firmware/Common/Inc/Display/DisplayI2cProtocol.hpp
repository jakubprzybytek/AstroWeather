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

// The host's timeline position, sent to the general-call address 0x00 so
// every board takes it at the same instant (see Display/Timeline.hpp): the
// command, the frame (32 bits) and the microseconds into it (16 bits), both
// little-endian. Not a 36-byte message: a board takes it only on the general
// call, never on its own address.
constexpr uint8_t kSyncCommand = 0x05U;
constexpr uint16_t kGeneralCallAddress = 0x00U;
constexpr std::size_t kSyncMessageSize = 7U;
using SyncMessage = std::array<uint8_t, kSyncMessageSize>;

// From the host's stamp to the board's address-match interrupt: the HAL's
// setup, START and the address byte with its ACK at 100 kHz (90 us), and the
// interrupt's entry. The seven data bytes come after the board's stamp, so
// an interrupt that holds the host up between them does not move it.
constexpr uint32_t kSyncTransferMicros = 130U;

// A board's answer to a one-byte read: 0xA0 with two flags. kStatusLocked:
// its timeline is on the host's (TimelineSync::locked()); clear, it wants a
// burst of syncs. kStatusNeedsContent: it has shown no content since it
// booted, or its content has expired (kContentLifetimeMs); the host sends its
// copy. A board without either answers 0x00. A host that knew only 0xA0 and
// 0xA1 reads the needs-content values as neither, so both boards go together.
constexpr uint8_t kStatusTag = 0xA0U;
constexpr uint8_t kStatusTagMask = 0xFCU;
constexpr uint8_t kStatusLocked = 0x01U;
constexpr uint8_t kStatusNeedsContent = 0x02U;

constexpr uint8_t boardStatus(bool locked, bool needsContent)
{
    return static_cast<uint8_t>(kStatusTag | (locked ? kStatusLocked : 0U) |
                                (needsContent ? kStatusNeedsContent : 0U));
}
constexpr bool statusValid(uint8_t status)
{
    return (status & kStatusTagMask) == kStatusTag;
}
constexpr bool statusWantsSyncs(uint8_t status)
{
    return statusValid(status) && (status & kStatusLocked) == 0U;
}
constexpr bool statusNeedsContent(uint8_t status)
{
    return statusValid(status) && (status & kStatusNeedsContent) != 0U;
}

// How long a display board shows content after the last frame before it
// shows "no data" and asks again; the host re-sends nothing older.
constexpr uint32_t kContentLifetimeMs = 7UL * 60UL * 60UL * 1000UL;

void serializeSync(uint32_t frame, uint16_t micros, SyncMessage& message);
// False, and both outputs untouched, unless it is a whole sync message.
bool deserializeSync(const uint8_t* data, std::size_t size, uint32_t& frame, uint16_t& micros);
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
