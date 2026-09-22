#include <Console/EepromCommand.hpp>

#include <Debug/LogService.hpp>
#include <Device/Eeprom24AA04.hpp>

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

constexpr uint16_t kBytesPerLine = 16U;

// The console accepts 95-character lines, so 'eeprom write <offset> <hex>' can
// carry at most a few dozen bytes. Keep the buffers small; ConsoleService runs
// on a 2 KB stack.
constexpr uint16_t kMaxWritePayload = 32U;
constexpr size_t kMaxPayloadText = (kMaxWritePayload * 2U) + 1U;

bool parseHexDigit(char character, uint8_t& value)
{
    if (character >= '0' && character <= '9') {
        value = static_cast<uint8_t>(character - '0');
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        value = static_cast<uint8_t>(character - 'a' + 10);
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        value = static_cast<uint8_t>(character - 'A' + 10);
        return true;
    }
    return false;
}

// Accepts an even-length hex string with no separators, e.g. "A55A01".
bool parseHexPayload(const char* text, uint8_t* data, uint16_t capacity, uint16_t& size)
{
    const size_t length = std::strlen(text);
    if (length == 0U || (length % 2U) != 0U || (length / 2U) > capacity) {
        return false;
    }
    size = static_cast<uint16_t>(length / 2U);
    for (uint16_t index = 0U; index < size; ++index) {
        uint8_t high = 0U;
        uint8_t low = 0U;
        if (!parseHexDigit(text[index * 2U], high) || !parseHexDigit(text[(index * 2U) + 1U], low)) {
            return false;
        }
        data[index] = static_cast<uint8_t>((high << 4U) | low);
    }
    return true;
}

void emitHexLine(uint16_t offset, const uint8_t* data, uint16_t size)
{
    char line[(kBytesPerLine * 3U) + 8U];
    // Three digits: offsets run to 0x1FF.
    int written = std::snprintf(line, sizeof(line), "%03X:", static_cast<unsigned>(offset));
    for (uint16_t column = 0U; column < size; ++column) {
        written += std::snprintf(&line[written], sizeof(line) - static_cast<size_t>(written),
                                 " %02X", static_cast<unsigned>(data[column]));
    }
    LogService::instance().sendLine(line);
}

// Reads and prints one line at a time so no full-array buffer lands on the stack.
CommandResult dumpRange(Device::Eeprom24AA04& eeprom, uint16_t offset, uint16_t size)
{
    uint8_t buffer[kBytesPerLine];
    for (uint16_t index = 0U; index < size; index = static_cast<uint16_t>(index + kBytesPerLine)) {
        const uint16_t remaining = static_cast<uint16_t>(size - index);
        const uint16_t count = (remaining < kBytesPerLine) ? remaining : kBytesPerLine;
        if (eeprom.read(static_cast<uint16_t>(offset + index), buffer, count) != HAL_OK) {
            return CommandResult::Unavailable;
        }
        emitHexLine(static_cast<uint16_t>(offset + index), buffer, count);
    }
    return CommandResult::Ok;
}

} // namespace

CommandResult handleEepromCommand(const char* line, Device::Eeprom24AA04* eeprom)
{
    if (std::strncmp(line, "eeprom", 6U) != 0) {
        return CommandResult::NotHandled;
    }
    if (eeprom == nullptr) {
        return CommandResult::Unavailable;
    }

    if (std::strcmp(line, "eeprom probe") == 0) {
        if (eeprom->probe() != HAL_OK) {
            return CommandResult::Unavailable;
        }
        char message[64];
        std::snprintf(message, sizeof(message), "OK eeprom-probe addr=0x%02X size=%u page=%u",
                      static_cast<unsigned>(eeprom->address()),
                      static_cast<unsigned>(Device::Eeprom24AA04::kSize),
                      static_cast<unsigned>(Device::Eeprom24AA04::kPageSize));
        LogService::instance().sendLine(message);
        return CommandResult::Ok;
    }

    if (std::strcmp(line, "eeprom dump") == 0) {
        return dumpRange(*eeprom, 0U, Device::Eeprom24AA04::kSize);
    }

    // Offsets and lengths are hex, so an address read off a 'dump' line can be
    // typed straight back in. Decimal parsing here made 'read 70' mean 0x46.
    unsigned int offset = 0U;
    unsigned int length = 0U;
    const int readArguments = std::sscanf(line, "eeprom read %x %x", &offset, &length);
    if (readArguments >= 1) {
        if (readArguments == 1) {
            length = 1U;
        }
        if (length == 0U || offset >= Device::Eeprom24AA04::kSize ||
            length > (Device::Eeprom24AA04::kSize - offset)) {
            return CommandResult::InvalidArgument;
        }
        return dumpRange(*eeprom, static_cast<uint16_t>(offset), static_cast<uint16_t>(length));
    }

    char payloadText[kMaxPayloadText] = {};
    static_assert(kMaxPayloadText == 65U, "update the sscanf width below when the cap changes");
    if (std::sscanf(line, "eeprom write %x %64s", &offset, payloadText) == 2) {
        uint8_t payload[kMaxWritePayload];
        uint16_t size = 0U;
        if (!parseHexPayload(payloadText, payload, kMaxWritePayload, size)) {
            return CommandResult::InvalidArgument;
        }
        if (offset >= Device::Eeprom24AA04::kSize ||
            size > (Device::Eeprom24AA04::kSize - offset)) {
            return CommandResult::InvalidArgument;
        }
        if (eeprom->write(static_cast<uint16_t>(offset), payload, size) != HAL_OK) {
            return CommandResult::Unavailable;
        }
        char message[64];
        std::snprintf(message, sizeof(message), "OK eeprom-write offset=0x%03X bytes=%u",
                      offset, static_cast<unsigned>(size));
        LogService::instance().sendLine(message);
        return CommandResult::Ok;
    }

    if (std::strcmp(line, "eeprom scan") == 0) {
        unsigned found = 0U;
        char message[64];
        // Addressing our own slave address wedges the peripheral (START never
        // completes and BUSY latches), so step over it.
        const uint16_t ownAddress =
            static_cast<uint16_t>(eeprom->bus().handle().Init.OwnAddress1 >> 1U);
        for (uint16_t candidate = 0x08U; candidate <= 0x77U; ++candidate) {
            if (candidate == ownAddress) {
                continue;
            }
            if (eeprom->bus().isDeviceReady(candidate, 2U, 5U) == HAL_OK) {
                std::snprintf(message, sizeof(message), "OK eeprom-scan found=0x%02X",
                              static_cast<unsigned>(candidate));
                LogService::instance().sendLine(message);
                ++found;
            }
        }
        std::snprintf(message, sizeof(message), "OK eeprom-scan devices=%u", found);
        LogService::instance().sendLine(message);
        return CommandResult::Ok;
    }

    if (std::strcmp(line, "eeprom erase") == 0) {
        uint8_t blank[Device::Eeprom24AA04::kPageSize];
        std::memset(blank, 0xFF, sizeof(blank));
        for (uint16_t page = 0U; page < Device::Eeprom24AA04::kSize;
             page = static_cast<uint16_t>(page + Device::Eeprom24AA04::kPageSize)) {
            if (eeprom->write(page, blank, Device::Eeprom24AA04::kPageSize) != HAL_OK) {
                return CommandResult::Unavailable;
            }
        }
        LogService::instance().sendLine("OK eeprom-erase");
        return CommandResult::Ok;
    }

    return CommandResult::InvalidArgument;
}

} // namespace Console
