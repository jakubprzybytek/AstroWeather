#pragma once

#include <Utils/Task.hpp>
#include <Display/Display.hpp>

#include "FreeRTOS.h"
#include "queue.h"

#include <cstdint>

class ConsoleService : public Task<2048>
{
public:
    static ConsoleService& instance();

    void init(Display::Display* display);
    void onUsbRxData(const uint8_t* data, uint32_t len);

protected:
    void run() override;

private:
    ConsoleService();

    static constexpr uint32_t kMaxLineLength = 96U;
    static constexpr uint32_t kRxRingSize = 256U;
    static constexpr uint32_t kCommandQueueDepth = 8U;
    static constexpr uint32_t kFlagCommand = 1U << 0;

    struct CommandLine
    {
        char text[kMaxLineLength];
    };

    void drainRxRing();
    void handleByte(uint8_t byte);
    void dispatchLine();
    void execute(const char* line);
    void reply(const char* format, ...);

    osMessageQueueId_t commandQueueHandle_;
    StaticQueue_t commandQueueCb_;
    uint8_t commandQueueStorage_[kCommandQueueDepth * sizeof(CommandLine)];
    volatile uint8_t rxRing_[kRxRingSize];
    volatile uint32_t rxHead_;
    volatile uint32_t rxTail_;
    char line_[kMaxLineLength];
    uint32_t lineLength_;
    bool lineTruncated_;
    Display::Display* display_;
};

extern "C" void ConsoleService_OnUsbRxData(const uint8_t* data, uint32_t len);
