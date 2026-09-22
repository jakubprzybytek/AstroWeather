#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

class Led;

class MainLoopTask : public Task<1536>
{
public:
    static MainLoopTask& instance();

    static constexpr uint32_t kEventSwitch1 = 1U << 0;
    static constexpr uint32_t kEventSwitch2 = 1U << 1;

    void init(Led& led);

protected:
    void run() override;

private:
    MainLoopTask();

    Led* led_ = nullptr;
};