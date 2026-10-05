#pragma once

#include <Display/DisplayTypes.hpp>

#include <array>

namespace Display {

class DisplayBoard {
public:
    virtual ~DisplayBoard() = default;
    NumericDisplay numeric(uint8_t index) { return NumericDisplay(state_.numeric[index]); }
    MatrixRow matrix(uint8_t row) { return MatrixRow(state_.matrix[row]); }
    const LogicalBoardState& state() const { return state_; }
    // Replaces the whole logical state; call submit() to show it.
    void setState(const LogicalBoardState& state) { state_ = state; }
    // Blink and level planes, kept separately from the content and shown
    // together with it by submit(). Default: nothing blinks, everything full.
    BoardAttributes& attributes() { return attributes_; }
    const BoardAttributes& attributes() const { return attributes_; }
    void setAttributes(const BoardAttributes& attributes) { attributes_ = attributes; }
    virtual void submit() = 0;
    // Shows `state` with `attributes` at once, leaving the board's own state
    // and attributes alone, so the next submit() brings them back. For the
    // boot screens; a board that only forwards over I2C ignores it.
    virtual void show(const LogicalBoardState&, const BoardAttributes&) {}

    // For status reporting. Remote boards sit on I2C and may be absent, so they
    // answer whether they respond right now; the local board is wired directly,
    // has no bus address, and is always present.
    virtual bool present() { return true; }
    virtual uint16_t address() const { return 0U; }

    // Counters of a board that refreshes LEDs itself; false for one that only
    // forwards its state over I2C.
    struct RefreshStats {
        uint32_t frames;
        // Refresh interrupts that found the previous pass's shift still
        // running and kept the old data: the interrupt was later than a pass.
        uint32_t lateShifts;
        // Refresh interrupts later than the pass they start, which was then
        // restarted rather than left to the timer's full range.
        uint32_t lateInterrupts;
        // Longest run of the refresh interrupt, from SysTick.
        uint32_t maxInterruptMicros;
        // The pass lengths in use, percent of a slot; see DisplayCodec.hpp.
        std::array<uint8_t, 4> passPercent;
    };
    virtual bool refreshStats(RefreshStats&) const { return false; }
    // Changes the pass lengths, for tuning the levels by eye. False when the
    // board does not refresh LEDs or the table is invalid.
    virtual bool setPassPercent(const std::array<uint8_t, 4>&) { return false; }

protected:
    LogicalBoardState state_{};
    BoardAttributes attributes_{};
};

} // namespace Display
