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

struct NumericSegments {
    std::array<uint8_t, kSlotCount> slots{};
};

struct LogicalBoardState {
    std::array<NumericSegments, kNumericDisplayCount> numeric{};
    std::array<uint32_t, kMatrixRowCount> matrix{};
};

using I2cMessage = std::array<uint8_t, kI2cMessageSize>;
using PreparedFrame = std::array<uint8_t, kSlotCount * kBytesPerSlot>;

class NumericDisplay {
public:
    explicit NumericDisplay(NumericSegments& data) : data_(data) {}

    void setFixed(int16_t mantissa, uint8_t precision = 0);
    void setValue(int16_t value);
    void setValue(float value, uint8_t precision = 0);
    void setTime(uint8_t hour, uint8_t minute);
    void setBlank();
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

class DisplayBoardState {
public:
    NumericDisplay numeric(uint8_t index) { return NumericDisplay(state_.numeric[index]); }
    MatrixRow matrix(uint8_t row) { return MatrixRow(state_.matrix[row]); }
    const LogicalBoardState& state() const { return state_; }
    LogicalBoardState& state() { return state_; }

private:
    LogicalBoardState state_{};
};

} // namespace Display
