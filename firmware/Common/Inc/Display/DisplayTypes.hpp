#pragma once

#include <array>
#include <cstdint>

namespace Display {

constexpr uint8_t kNumericDisplayCount = 4;
constexpr uint8_t kMatrixRowCount = 5;
constexpr uint8_t kMatrixColumnCount = 21;
constexpr uint8_t kSlotCount = 5;
constexpr uint8_t kBytesPerSlot = 7;
constexpr uint8_t kBytesPerNumericDisplay = kSlotCount;
constexpr uint8_t kLogicalPayloadSize =
    kNumericDisplayCount * kBytesPerNumericDisplay + kMatrixRowCount * 3U;
constexpr uint8_t kI2cMessageSize = 1U + kLogicalPayloadSize;
constexpr uint8_t kMaxPrecision = 3;
constexpr uint32_t kMatrixMask = (1UL << kMatrixColumnCount) - 1UL;
constexpr uint8_t kSegmentDp = 1U << 7U;

struct NumericSegments {
    std::array<uint8_t, kSlotCount> slots{};
};

struct LogicalBoardState {
    std::array<NumericSegments, kNumericDisplayCount> numeric{};
    std::array<uint32_t, kMatrixRowCount> matrix{};
};

using I2cMessage = std::array<uint8_t, kI2cMessageSize>;
using PreparedFrame = std::array<uint8_t, kSlotCount * kBytesPerSlot>;

// Brightness levels. Every lit segment and pixel has a level 0 to 3; 3 is
// full and the default. The refresh shows a level as some of the passes a
// slot is split into; see kLevelPasses in DisplayCodec.hpp.
constexpr uint8_t kLevelBits = 2;
constexpr uint8_t kLevelCount = 1U << kLevelBits;
constexpr uint8_t kLevelFull = kLevelCount - 1U;

// A LogicalBoardState doubles as a bit-plane: one bit per segment and per
// pixel. These act elementwise; ~ keeps the unused matrix bits 21-23 zero.
LogicalBoardState operator&(const LogicalBoardState& a, const LogicalBoardState& b);
LogicalBoardState operator|(const LogicalBoardState& a, const LogicalBoardState& b);
LogicalBoardState operator~(const LogicalBoardState& a);
bool operator==(const LogicalBoardState& a, const LogicalBoardState& b);
bool operator!=(const LogicalBoardState& a, const LogicalBoardState& b);

// Every segment and pixel bit set.
constexpr LogicalBoardState allElements()
{
    LogicalBoardState state{};
    for (NumericSegments& numeric : state.numeric) {
        for (uint8_t& slot : numeric.slots) {
            slot = 0xFFU;
        }
    }
    for (uint32_t& row : state.matrix) {
        row = kMatrixMask;
    }
    return state;
}

// Per-element display effects, one bit-plane each, in the same layout as the
// content so the same encoder and I2C serialiser handle them. Attributes
// persist across content updates: set the clock's colon to blink once, then
// keep calling setTime(). An attribute on an unlit element has no effect.
struct BoardAttributes {
    LogicalBoardState blink{};                 // set: shown in the blink-on phase only
    LogicalBoardState level0 = allElements();  // bit 0 of the element's level
    LogicalBoardState level1 = allElements();  // bit 1; both set is full, the default

    // Masks use the normalized segment layout of NumericSegments and the
    // column bits of setRow(). An index or row out of range is ignored; a
    // level above kLevelFull is treated as full.
    void setNumericBlink(uint8_t index, const NumericSegments& mask);
    void setMatrixBlink(uint8_t row, uint32_t columns);
    void clearBlink();
    void setNumericLevel(uint8_t index, uint8_t level);  // all five slots
    void setNumericLevel(uint8_t index, const NumericSegments& mask, uint8_t level);
    void setMatrixLevel(uint8_t row, uint32_t columns, uint8_t level);
    void clearLevels();  // everything back to full

    uint8_t numericLevel(uint8_t index, uint8_t slot, uint8_t segment) const;
    uint8_t matrixLevel(uint8_t row, uint8_t column) const;
};

class NumericDisplay {
public:
    explicit NumericDisplay(NumericSegments& data) : data_(data) {}

    void setFixed(int16_t mantissa, uint8_t precision = 0);
    void setValue(int16_t value);
    void setValue(float value, uint8_t precision = 0);
    void setTime(uint8_t hour, uint8_t minute);
    // "--:--", for a clock that has not been set.
    void setTimeUnset();
    void setBlank();
    // "   -": segment G on the last digit only, nothing else lit. Nothing to
    // show yet, or the data has gone stale.
    void setNoData();
    void setSegments(const NumericSegments& segments) { data_ = segments; }

private:
    void setError();
    NumericSegments& data_;
};

class MatrixRow {
public:
    explicit MatrixRow(uint32_t& value) : value_(value) {}
    void setRow(uint32_t columns) { value_ = columns & kMatrixMask; }

private:
    uint32_t& value_;
};

// A whole board in the "no data" state: every numeric display "   -", the
// matrix blank. Shown at boot on every board until the first data arrives.
LogicalBoardState noDataState();

// Content and attributes without a board, for tests and pure code.
class DisplayBoardState {
public:
    NumericDisplay numeric(uint8_t index) { return NumericDisplay(state_.numeric[index]); }
    MatrixRow matrix(uint8_t row) { return MatrixRow(state_.matrix[row]); }
    const LogicalBoardState& state() const { return state_; }
    LogicalBoardState& state() { return state_; }
    BoardAttributes& attributes() { return attributes_; }
    const BoardAttributes& attributes() const { return attributes_; }

private:
    LogicalBoardState state_{};
    BoardAttributes attributes_{};
};

} // namespace Display
