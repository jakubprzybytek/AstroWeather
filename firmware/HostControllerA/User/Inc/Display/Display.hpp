#pragma once

#include <Display/BoardChain.hpp>
#include <Display/DisplayBoard.hpp>
#include <Utils/Mutex.hpp>

#include <array>

namespace Display {

// Setters (via local()/board() DisplayBoard accessors) only touch buffered logical
// state and need no synchronization. submit() drives SPI/I2C transfers and is the
// only method that requires mutual exclusion between client tasks.
//
// The boards are addressed by chain position (BoardChain.hpp): position n is the
// board at 0x10 + n. There is a remote board for every position; the one at the
// host's own address is replaced by the local board and never used.
class Display {
public:
    Display(DisplayBoard& local, const std::array<DisplayBoard*, kChainLength>& remote)
        : local_(local), remote_(remote) {}

    // The host's address from its straps; call once at boot, before the first
    // submit(). Until then the host is taken to be 0x10, a host with no straps.
    void setLocalAddress(uint16_t address)
    {
        localAddress_ = address;
        localPosition_ = chainPosition(address);
    }
    uint16_t localAddress() const { return localAddress_; }
    bool localInChain() const { return localPosition_ != kNotInChain; }

    DisplayBoard& local() { return local_; }
    bool isLocal(uint8_t position) const { return position == localPosition_; }
    // The board at chain position `position` (< kChainLength): the local board
    // at the host's own address, otherwise the remote one.
    DisplayBoard& board(uint8_t position)
    {
        return isLocal(position) ? local_ : *remote_[position];
    }
    // The remote board at `position`, or nullptr for the host's own address,
    // an empty slot or a position out of range.
    DisplayBoard* remoteBoard(uint8_t position)
    {
        return (position < kChainLength && !isLocal(position)) ? remote_[position] : nullptr;
    }

    void submit();
    // Refreshes only the local board, under the same lock as submit(). For
    // frequent local-only changes, such as refresh progress, that should not
    // cost an I2C transfer to every remote board.
    void submitLocal();
    // Refreshes only the remote board at chain position `position`, under the
    // same lock as submit(). For the console's per-board commands. False for
    // a position with no remote board, or a board that did not take it.
    bool submitRemote(uint8_t position);

    // Sends the remote board at `position` the state it holds again, for a
    // board that asks for content after a reset (DisplaySyncTask). Only what
    // a client has submitted since boot, and not older than a board shows it
    // (kContentLifetimeMs): a board is better left on "no data" than shown
    // blanks or expired data.
    enum class Resend : uint8_t { Sent, Failed, NothingToSend };
    Resend resendRemote(uint8_t position, uint32_t nowMs);

    // The boot screens on the local board (Display/BootScreens.hpp), about
    // 3 s, then its own state. Until this returns, submit() and submitLocal()
    // leave the local board out: clients keep writing its state, which shows
    // when the boot screens end. Call once, from a task.
    void runBootScreens();

private:
    DisplayBoard& local_;
    std::array<DisplayBoard*, kChainLength> remote_;
    uint16_t localAddress_ = kChainFirstAddress;
    uint8_t localPosition_ = 0U;
    // Set from construction, so nothing reaches the local board before the
    // boot screens have run; guarded by submitMutex_.
    bool bootScreens_ = true;
    // When a client last submitted each remote board (kernel ms); 0 for never.
    // Guarded by submitMutex_.
    std::array<uint32_t, kChainLength> submittedAt_{};
    Mutex submitMutex_;

    void noteSubmitted(uint8_t position);
};

} // namespace Display
