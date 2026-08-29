#pragma once

#include <Display/DisplayBoard.hpp>

#include <array>

namespace Display {

class Display {
public:
    Display(DisplayBoard& local, const std::array<DisplayBoard*, 4>& remote)
        : local_(local), remote_(remote) {}

    DisplayBoard& local() { return local_; }
    DisplayBoard& remote(uint8_t index) { return *remote_[index]; }
    void submit();

private:
    DisplayBoard& local_;
    std::array<DisplayBoard*, 4> remote_;
};

} // namespace Display
