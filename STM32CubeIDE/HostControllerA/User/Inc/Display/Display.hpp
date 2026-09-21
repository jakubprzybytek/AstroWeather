#pragma once

#include <Display/DisplayBoard.hpp>
#include <Utils/Mutex.hpp>

#include <array>

namespace Display {

// Setters (via local()/remote() DisplayBoard accessors) only touch buffered logical
// state and need no synchronization. submit() drives SPI/I2C transfers and is the
// only method that requires mutual exclusion between client tasks.
class Display {
public:
    static constexpr uint8_t kRemoteSlots = 5U;

    Display(DisplayBoard& local, const std::array<DisplayBoard*, kRemoteSlots>& remote)
        : local_(local), remote_(remote) {}

    DisplayBoard& local() { return local_; }
    DisplayBoard& remote(uint8_t index) { return *remote_[index]; }

    // Remote board in slot index, or nullptr if the slot is empty or out of range.
    DisplayBoard* remoteSlot(uint8_t index)
    {
        return (index < kRemoteSlots) ? remote_[index] : nullptr;
    }
    void submit();

private:
    DisplayBoard& local_;
    std::array<DisplayBoard*, kRemoteSlots> remote_;
    Mutex submitMutex_;
};

} // namespace Display
