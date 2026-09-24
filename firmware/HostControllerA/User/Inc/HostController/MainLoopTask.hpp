#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

class Led;

namespace Settings {
class Store;
}

class MainLoopTask : public Task<1536>
{
public:
    static MainLoopTask& instance();

    static constexpr uint32_t kEventSwitch1 = 1U << 0;
    static constexpr uint32_t kEventSwitch2 = 1U << 1;

    // `settings` may be null; switch 2 then changes brightness without saving it.
    void init(Led& led, Settings::Store* settings);

protected:
    void run() override;

private:
    MainLoopTask();

    Led* led_ = nullptr;
    Settings::Store* settings_ = nullptr;
};