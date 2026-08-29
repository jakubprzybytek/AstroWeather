#pragma once

#include <array>
#include <cstdint>

namespace Display {

constexpr uint8_t kNumericDisplayCount = 4;
constexpr uint8_t kMatrixRowCount = 5;
constexpr uint8_t kMatrixColumnCount = 21;
constexpr uint8_t kSlotCount = 5;
constexpr uint8_t kBytesPerSlot = 7;
constexpr uint8_t kLogicalPayloadSize = 27;
constexpr uint8_t kI2cMessageSize = 28;
constexpr uint8_t kMaxPrecision = 3;
constexpr uint32_t kMatrixMask = (1UL << kMatrixColumnCount) - 1UL;

enum class NumericMode : uint8_t {
    Blank = 0,
    Value = 1,
    Time = 2,
    Error = 3,
};

constexpr uint8_t kModeMask = 0x03U;
constexpr uint8_t kPrecisionMask = 0x0CU;
constexpr uint8_t kDoubleDotsMask = 0x10U;

struct NumericData {
    int16_t mantissa = 0;
    uint8_t flags = 0;
};

struct LogicalBoardState {
    std::array<NumericData, kNumericDisplayCount> numeric{};
    std::array<uint32_t, kMatrixRowCount> matrix{};
};

using I2cMessage = std::array<uint8_t, kI2cMessageSize>;
using PreparedFrame = std::array<uint8_t, kSlotCount * kBytesPerSlot>;

class NumericDisplay {
public:
    explicit NumericDisplay(NumericData& data) : data_(data) {}

    void setFixed(int16_t mantissa, uint8_t precision = 0);
    void setValue(int16_t value);
    void setValue(float value, uint8_t precision = 0);
    void setTime(uint8_t hour, uint8_t minute);
    void setBlank();

private:
    void setError();
    NumericData& data_;
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

inline NumericMode modeOf(NumericData data)
{
    return static_cast<NumericMode>(data.flags & kModeMask);
}

inline uint8_t precisionOf(NumericData data)
{
    return static_cast<uint8_t>((data.flags & kPrecisionMask) >> 2U);
}

inline bool doubleDotsOf(NumericData data)
{
    return (data.flags & kDoubleDotsMask) != 0U;
}

} // namespace Display
