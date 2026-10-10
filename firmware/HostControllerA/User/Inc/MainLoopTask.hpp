#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

class PulseLed;

namespace Settings {
class Store;
}

namespace Display {
class Display;
}

// 2048 B: the boot screens encode frames on this stack (show() ->
// encodePasses(), about 1 KB on the DisplayController with the context switch
// frame).
class MainLoopTask : public Task<2048>
{
public:
    static MainLoopTask& instance();

    static constexpr uint32_t kEventSwitch1 = 1U << 0;
    static constexpr uint32_t kEventSwitch2 = 1U << 1;

    // `settings` may be null; switch 2 then changes brightness without saving it.
    // `display` may be null; the boot screens are then skipped.
    // `startup` runs first, once the scheduler is up: the start-up steps that
    // need I2C, whose timeouts only run then. May be null.
    void init(PulseLed& led, Settings::Store* settings, Display::Display* display,
              void (*startup)());

protected:
    void run() override;

private:
    MainLoopTask();

    PulseLed* led_ = nullptr;
    Settings::Store* settings_ = nullptr;
    Display::Display* display_ = nullptr;
    void (*startup_)() = nullptr;
};