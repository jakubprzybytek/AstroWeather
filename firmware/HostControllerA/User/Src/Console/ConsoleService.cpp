#include <Console/ConsoleService.hpp>

#include <Console/AdcCommand.hpp>
#if defined(FIRMWARE_VARIANT_HostController)
#include <Console/AstroCommand.hpp>
#endif
#include <Console/DisplayCommand.hpp>
#include <Console/EepromCommand.hpp>
#include <Console/HelpCommand.hpp>
#include <Console/SettingsCommand.hpp>
#include <Console/StatusCommand.hpp>
#if defined(FIRMWARE_VARIANT_HostController)
#include <Console/TimeCommand.hpp>
#endif
#include <Debug/FirmwareInfo.hpp>
#include <Settings/SettingsStore.hpp>
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
    lineTruncated_(false), display_(nullptr), eeprom_(nullptr), settings_(nullptr)
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

void ConsoleService::setEeprom(Device::Eeprom24AA04* eeprom)
{
    eeprom_ = eeprom;
}

void ConsoleService::onHostLineState(bool dataTerminalReady)
{
    // Called from the USB interrupt. Terminals raise DTR when they open the
    // port, so a rising edge means someone has just connected.
    const bool connected = dataTerminalReady && !hostDtr_;
    hostDtr_ = dataTerminalReady;
    const osThreadId_t handle = getHandle();
    if (connected && handle != nullptr) {
        osThreadFlagsSet(handle, kFlagHostConnected);
    }
}

void ConsoleService::onHostLineCoding()
{
    // Called from the USB interrupt. There is no edge to track: any terminal
    // opening the port sets the line coding, so each one is a candidate
    // connect, and run() folds repeats into one welcome.
    const osThreadId_t handle = getHandle();
    if (handle != nullptr) {
        osThreadFlagsSet(handle, kFlagHostConnected);
    }
}

void ConsoleService::sendWelcome()
{
    reply("OK connected to AstroWeather %s, built %s", firmwareVariant(), firmwareBuildTime());
    // The startup log is emitted before USB has enumerated and never reaches
    // the host, so restate the one boot-time result worth knowing.
    if (settings_ != nullptr) {
        reply("Settings loaded from EEPROM: %s",
              Settings::Store::describe(settings_->lastDecode()));
    }
    reply("Type 'help' for commands. Periodic stats are %s; 'stats %s' to switch.",
          LogService::instance().statsEnabled() ? "on" : "off",
          LogService::instance().statsEnabled() ? "off" : "on");
}

void ConsoleService::setSettings(Settings::Store* settings)
{
    settings_ = settings;
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
    if (Console::handleHelpCommand(line) == Console::CommandResult::Ok) {
        return;
    }
    if (Console::handleStatusCommand(line, display_, eeprom_, settings_) ==
        Console::CommandResult::Ok) {
        return;
    }
    if (std::strcmp(line, "stats on") == 0 || std::strcmp(line, "stats off") == 0) {
        const bool enabled = std::strcmp(line, "stats on") == 0;
        // Reply first: switching on emits a report straight away, which would
        // otherwise land ahead of the acknowledgement.
        reply(enabled ? "OK stats=on" : "OK stats=off");
        LogService::instance().setStatsEnabled(enabled);
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

    const Console::CommandResult timeResult = Console::handleTimeCommand(line, settings_);
    if (timeResult == Console::CommandResult::Ok) {
        return;
    }
    if (timeResult == Console::CommandResult::Unavailable) {
        reply("ERR rtc-unavailable");
        return;
    }
    if (timeResult == Console::CommandResult::InvalidArgument) {
        reply("ERR invalid-argument");
        return;
    }
#endif
    const Console::CommandResult adcResult = Console::handleAdcCommand(line, settings_);
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

    const Console::CommandResult settingsResult =
        Console::handleSettingsCommand(line, settings_);
    if (settingsResult == Console::CommandResult::Ok) {
        return;
    }
    if (settingsResult == Console::CommandResult::Unavailable) {
        reply("ERR settings-unavailable");
        return;
    }
    if (settingsResult == Console::CommandResult::InvalidArgument) {
        reply("ERR invalid-argument");
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
        const uint32_t flags =
            osThreadFlagsWait(kFlagCommand | kFlagHostConnected, osFlagsWaitAny, osWaitForever);
        if ((flags & osFlagsError) != 0U) {
            continue;
        }
        if ((flags & kFlagHostConnected) != 0U) {
            // Opening a port can raise DTR and set the line coding within a few
            // milliseconds of each other; one welcome per open is enough.
            const uint32_t now = osKernelGetTickCount();
            if (!welcomed_ || (now - lastWelcomeTick_) >= kWelcomeHoldoffMs) {
                welcomed_ = true;
                lastWelcomeTick_ = now;
                sendWelcome();
            }
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

extern "C" void ConsoleService_OnHostLineState(uint8_t dataTerminalReady)
{
    ConsoleService::instance().onHostLineState(dataTerminalReady != 0U);
}

extern "C" void ConsoleService_OnHostLineCoding(void)
{
    ConsoleService::instance().onHostLineCoding();
}
