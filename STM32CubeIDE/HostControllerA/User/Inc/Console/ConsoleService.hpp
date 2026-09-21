#pragma once

#include <Utils/Task.hpp>
#include <Display/Display.hpp>

#include "FreeRTOS.h"
#include "queue.h"

#include <cstdint>

namespace Device {
class Eeprom24AA04;
}

namespace Settings {
class Store;
}

class ConsoleService : public Task<2048>
{
public:
    static ConsoleService& instance();

    void init(Display::Display* display);
    void setEeprom(Device::Eeprom24AA04* eeprom);
    void setSettings(Settings::Store* settings);
    void onUsbRxData(const uint8_t* data, uint32_t len);
    // USB CDC SET_CONTROL_LINE_STATE, from interrupt context.
    void onHostLineState(bool dataTerminalReady);

protected:
    void run() override;

private:
    ConsoleService();

    // Long enough for 'wifi set' with a quoted 32-character SSID and a quoted
    // 63-character passphrase.
    static constexpr uint32_t kMaxLineLength = 128U;
    static constexpr uint32_t kRxRingSize = 256U;
    static constexpr uint32_t kCommandQueueDepth = 8U;
    static constexpr uint32_t kFlagCommand = 1U << 0;
    static constexpr uint32_t kFlagHostConnected = 1U << 1;

    struct CommandLine
    {
        char text[kMaxLineLength];
    };

    void drainRxRing();
    void handleByte(uint8_t byte);
    void dispatchLine();
    void execute(const char* line);
    void reply(const char* format, ...);
    void sendWelcome();

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
    Device::Eeprom24AA04* eeprom_;
    Settings::Store* settings_;
    volatile bool hostDtr_ = false;
};

extern "C" void ConsoleService_OnUsbRxData(const uint8_t* data, uint32_t len);
