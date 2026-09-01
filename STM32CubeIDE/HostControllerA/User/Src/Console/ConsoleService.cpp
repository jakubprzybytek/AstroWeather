#include <Console/ConsoleService.hpp>

#include <Debug/DebugService.hpp>

#include "cmsis_os2.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

enum class DisplayCommandStatus : uint8_t {
    Ok,
    Unavailable,
    InvalidArgument,
};

} // namespace

ConsoleService& ConsoleService::instance()
{
    static ConsoleService service;
    return service;
}

ConsoleService::ConsoleService()
    : Task<2048>("ConsoleService", osPriorityNormal),
      commandQueueHandle_(nullptr), commandQueueCb_{}, commandQueueStorage_{},
      rxRing_{}, rxHead_(0U), rxTail_(0U), line_{}, lineLength_(0U),
    lineTruncated_(false), display_(nullptr)
{
}

void ConsoleService::init(Display::Display* display)
{
    display_ = display;
    osMessageQueueAttr_t attr = {};
    attr.name = "ConsoleCmdQ";
    attr.cb_mem = &commandQueueCb_;
    attr.cb_size = sizeof(commandQueueCb_);
    attr.mq_mem = commandQueueStorage_;
    attr.mq_size = sizeof(commandQueueStorage_);
    commandQueueHandle_ = osMessageQueueNew(kCommandQueueDepth, sizeof(CommandLine), &attr);
}

void ConsoleService::onUsbRxData(const uint8_t* data, uint32_t len)
{
    for (uint32_t index = 0U; index < len; ++index) {
        if ((rxHead_ - rxTail_) >= kRxRingSize) {
            continue;
        }
        rxRing_[rxHead_ % kRxRingSize] = data[index];
        ++rxHead_;
    }
    osThreadFlagsSet(getHandle(), kFlagCommand);
}

void ConsoleService::drainRxRing()
{
    while (rxTail_ != rxHead_) {
        const uint8_t byte = rxRing_[rxTail_ % kRxRingSize];
        ++rxTail_;
        handleByte(byte);
    }
}

void ConsoleService::handleByte(uint8_t byte)
{
    if (byte == '\r') {
        return;
    }
    if (byte == '\n') {
        if (lineTruncated_) {
            reply("ERR line-too-long");
        } else if (lineLength_ != 0U) {
            dispatchLine();
        }
        lineLength_ = 0U;
        lineTruncated_ = false;
        return;
    }
    if (lineLength_ < (kMaxLineLength - 1U)) {
        line_[lineLength_++] = static_cast<char>(byte);
    } else {
        lineTruncated_ = true;
    }
}

void ConsoleService::dispatchLine()
{
    CommandLine command{};
    std::memcpy(command.text, line_, lineLength_);
    command.text[lineLength_] = '\0';
    if (commandQueueHandle_ == nullptr || osMessageQueuePut(commandQueueHandle_, &command, 0U, 0U) != osOK) {
        reply("ERR command-queue-full");
        return;
    }
    osThreadFlagsSet(getHandle(), kFlagCommand);
}

void ConsoleService::reply(const char* format, ...)
{
    char message[128];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    DebugService::instance().sendLine(message);
}

void ConsoleService::execute(const char* line)
{
    if (std::strcmp(line, "help") == 0) {
        reply("OK 'help' - show commands, example: 'help'");
        reply("OK 'status' - show system status, example: 'status'");
        reply("OK 'display set' - set value and precision, example: 'display set 0 1234 2'");
        reply("OK 'display time' - set hour and minute, example: 'display time 0 12:34'");
        reply("OK 'display blank' - clear a display, example: 'display blank 0'");
        return;
    }
    if (std::strcmp(line, "status") == 0) {
        reply("OK status=ready");
        return;
    }

    unsigned int index = 0U;
    int value = 0;
    unsigned int precision = 0U;
    unsigned int hour = 0U;
    unsigned int minute = 0U;
    DisplayCommandStatus status = DisplayCommandStatus::InvalidArgument;

    if (std::sscanf(line, "display set %u %d %u", &index, &value, &precision) == 3) {
        const int32_t magnitude = value < 0 ? -static_cast<int32_t>(value) : value;
        const bool valueValid = value >= std::numeric_limits<int16_t>::min() &&
                                value <= std::numeric_limits<int16_t>::max() &&
                                value != std::numeric_limits<int16_t>::min() &&
                                (value < 0 ? magnitude <= 999 : magnitude <= 9999);
        if (display_ != nullptr && index < Display::kNumericDisplayCount &&
            precision <= Display::kMaxPrecision && valueValid) {
            display_->local().numeric(static_cast<uint8_t>(index)).setFixed(
                static_cast<int16_t>(value), static_cast<uint8_t>(precision));
            display_->submit();
            status = DisplayCommandStatus::Ok;
        } else if (display_ == nullptr) {
            status = DisplayCommandStatus::Unavailable;
        }
    } else if (std::sscanf(line, "display time %u %u:%u", &index, &hour, &minute) == 3) {
        if (display_ != nullptr && index < Display::kNumericDisplayCount &&
            hour <= 99U && minute <= 99U) {
            display_->local().numeric(static_cast<uint8_t>(index)).setTime(
                static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));
            display_->submit();
            status = DisplayCommandStatus::Ok;
        } else if (display_ == nullptr) {
            status = DisplayCommandStatus::Unavailable;
        }
    } else if (std::sscanf(line, "display blank %u", &index) == 1) {
        if (display_ != nullptr && index < Display::kNumericDisplayCount) {
            display_->local().numeric(static_cast<uint8_t>(index)).setBlank();
            display_->submit();
            status = DisplayCommandStatus::Ok;
        } else if (display_ == nullptr) {
            status = DisplayCommandStatus::Unavailable;
        }
    } else {
        reply("ERR invalid-command");
        return;
    }

    if (status == DisplayCommandStatus::Ok) {
        reply("OK display");
    } else if (status == DisplayCommandStatus::Unavailable) {
        reply("ERR display-unavailable");
    } else {
        reply("ERR invalid-argument");
    }
}

void ConsoleService::run()
{
    for (;;) {
        const uint32_t flags = osThreadFlagsWait(kFlagCommand, osFlagsWaitAny, osWaitForever);
        if ((flags & osFlagsError) != 0U) {
            continue;
        }
        drainRxRing();
        CommandLine command{};
        while (commandQueueHandle_ != nullptr &&
               osMessageQueueGet(commandQueueHandle_, &command, nullptr, 0U) == osOK) {
            execute(command.text);
        }
    }
}

extern "C" void ConsoleService_OnUsbRxData(const uint8_t* data, uint32_t len)
{
    ConsoleService::instance().onUsbRxData(data, len);
}
