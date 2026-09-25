#pragma once

#include <Display/DisplayTypes.hpp>

namespace Display {

class DisplayBoard {
public:
    virtual ~DisplayBoard() = default;
    NumericDisplay numeric(uint8_t index) { return NumericDisplay(state_.numeric[index]); }
    MatrixRow matrix(uint8_t row) { return MatrixRow(state_.matrix[row]); }
    const LogicalBoardState& state() const { return state_; }
    // Replaces the whole logical state; call submit() to show it.
    void setState(const LogicalBoardState& state) { state_ = state; }
    virtual void submit() = 0;

    // For status reporting. Remote boards sit on I2C and may be absent, so they
    // answer whether they respond right now; the local board is wired directly,
    // has no bus address, and is always present.
    virtual bool present() { return true; }
    virtual uint16_t address() const { return 0U; }

protected:
    LogicalBoardState state_{};
};

} // namespace Display
