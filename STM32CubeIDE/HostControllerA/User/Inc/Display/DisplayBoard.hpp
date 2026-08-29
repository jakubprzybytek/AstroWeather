#pragma once

#include <Display/DisplayTypes.hpp>

namespace Display {

class DisplayBoard {
public:
    virtual ~DisplayBoard() = default;
    NumericDisplay numeric(uint8_t index) { return NumericDisplay(state_.numeric[index]); }
    MatrixRow matrix(uint8_t row) { return MatrixRow(state_.matrix[row]); }
    const LogicalBoardState& state() const { return state_; }
    virtual void submit() = 0;

protected:
    LogicalBoardState state_{};
};

} // namespace Display
