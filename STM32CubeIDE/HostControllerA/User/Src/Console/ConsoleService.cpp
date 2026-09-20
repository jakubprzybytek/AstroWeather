#include <Console/ConsoleService.hpp>

#include <Console/AdcCommand.hpp>
#if defined(FIRMWARE_VARIANT_HostController)
#include <Console/AstroCommand.hpp>
#endif
#include <Console/DisplayCommand.hpp>
#include <Console/EepromCommand.hpp>
#include <Debug/LogService.hpp>

#include "cmsis_os2.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

ConsoleService& ConsoleService::instance()
{
    static ConsoleService service;
    return service;
}

ConsoleService::ConsoleService()
    : Task<2048>("ConsoleService", osPriorityNormal),
      commandQueueHandle_(nullptr), commandQueueCb_{}, commandQueueStorage_{},
      rxRing_{}, rxHead_(0U), rxTail_(0U), line_{}, lineLength_(0U),
    lineTruncated_(false), display_(nullptr), eeprom_(nullptr)
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

void ConsoleService::setEeprom(Device::Eeprom24AA01* eeprom)
{
    eeprom_ = eeprom;
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
    LogService::instance().sendLine(message);
}

void ConsoleService::execute(const char* line)
{
    if (std::strcmp(line, "help") == 0) {
        reply("OK 'help' - show commands, example: 'help'");
        reply("OK 'status' - show system status, example: 'status'");
        reply("OK 'display set' - set value and precision, example: 'display set 0 1234 2'");
        reply("OK 'display time' - set hour and minute, example: 'display time 0 12:34'");
        reply("OK 'display blank' - clear a display, example: 'display blank 0'");
        reply("OK 'display matrix' - set binary pixels, example: 'display matrix 0 010101010101101100110'");
    #if defined(FIRMWARE_VARIANT_HostController)
        reply("OK 'astro refresh' - fetch and publish astro data, example: 'astro refresh'");
    #endif
        reply("OK 'adc log on' - enable current-sense readout logging, example: 'adc log on'");
        reply("OK 'adc log off' - disable current-sense readout logging, example: 'adc log off'");
        reply("OK 'adc display on' - enable current-sense readout on display, example: 'adc display on'");
        reply("OK 'adc display off' - disable current-sense readout on display, example: 'adc display off'");
        reply("OK 'eeprom probe' - check the settings EEPROM responds, example: 'eeprom probe'");
        reply("OK 'eeprom dump' - hex dump the whole EEPROM, example: 'eeprom dump'");
        reply("OK 'eeprom read' - hex dump a range, hex offset/length, example: 'eeprom read 70 10'");
        reply("OK 'eeprom write' - write hex bytes at a hex offset, example: 'eeprom write 00 A55A01'");
        reply("OK 'eeprom erase' - fill the EEPROM with 0xFF, example: 'eeprom erase'");
        return;
    }
    if (std::strcmp(line, "status") == 0) {
        reply("OK status=ready");
        return;
    }
#if defined(FIRMWARE_VARIANT_HostController)
    const Console::CommandResult astroResult = Console::handleAstroCommand(line);
    if (astroResult == Console::CommandResult::Ok) {
        reply("OK astro-refresh=started");
        return;
    }
    if (astroResult == Console::CommandResult::Busy) {
        reply("ERR astro-refresh-busy");
        return;
    }
    if (astroResult == Console::CommandResult::Unavailable) {
        reply("ERR astro-refresh-unavailable");
        return;
    }
    if (astroResult == Console::CommandResult::InvalidArgument) {
        reply("ERR invalid-argument");
        return;
    }
#endif
    const Console::CommandResult adcResult = Console::handleAdcCommand(line);
    if (adcResult == Console::CommandResult::Ok) {
        if (std::strcmp(line, "adc log on") == 0) {
            reply("OK adc-log=on");
        } else if (std::strcmp(line, "adc log off") == 0) {
            reply("OK adc-log=off");
        } else if (std::strcmp(line, "adc display on") == 0) {
            reply("OK adc-display=on");
        } else {
            reply("OK adc-display=off");
        }
        return;
    }
    if (adcResult != Console::CommandResult::NotHandled) {
        reply("ERR invalid-command");
        return;
    }

    const Console::CommandResult eepromResult = Console::handleEepromCommand(line, eeprom_);
    if (eepromResult == Console::CommandResult::Ok) {
        return;
    }
    if (eepromResult == Console::CommandResult::Unavailable) {
        reply("ERR eeprom-unavailable");
        return;
    }
    if (eepromResult == Console::CommandResult::InvalidArgument) {
        reply("ERR invalid-argument");
        return;
    }

    const Console::CommandResult displayResult = Console::handleDisplayCommand(line, display_);
    if (displayResult == Console::CommandResult::Ok) {
        reply("OK display");
    } else if (displayResult == Console::CommandResult::Unavailable) {
        reply("ERR display-unavailable");
    } else if (displayResult == Console::CommandResult::InvalidArgument) {
        reply("ERR invalid-argument");
    } else {
        reply("ERR invalid-command");
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
