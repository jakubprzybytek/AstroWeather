#include <Console/ErrorsCommand.hpp>

#include <Debug/LogService.hpp>

#include "cmsis_os2.h"

#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// A consistent copy to read while other tasks keep logging. Static, not on
// the console task's stack: it is about 2 KB. Console task only.
ErrorLog::Storage snapshot;

// The log queue holds 16 lines and the full list is up to 18; a short pause
// every few lines lets LogService drain it, so none is dropped.
constexpr uint8_t kLinesPerBurst = 8U;
constexpr uint32_t kBurstPauseMs = 30U;

void send(const char* line, uint8_t& sent)
{
    LogService::instance().sendLine(line);
    if (++sent % kLinesPerBurst == 0U) {
        osDelay(kBurstPauseMs);
    }
}

} // namespace

CommandResult handleErrorsCommand(const char* line)
{
    if (std::strcmp(line, "errors clear") == 0) {
        LogService::instance().clearErrorLog();
        LogService::instance().sendLine("OK errors cleared");
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "errors") != 0) {
        return std::strncmp(line, "errors ", 7U) == 0 ? CommandResult::InvalidArgument
                                                      : CommandResult::NotHandled;
    }

    LogService::instance().errorLogSnapshot(snapshot);
    const ErrorLog::Log log(snapshot);
    char text[200];
    uint8_t sent = 0U;
    std::snprintf(text, sizeof(text), "OK errors %u of %u kept, %lu older dropped, boot %lu",
                  static_cast<unsigned>(log.count()), static_cast<unsigned>(ErrorLog::kCapacity),
                  static_cast<unsigned long>(log.dropped()), static_cast<unsigned long>(log.boot()));
    send(text, sent);
    for (uint8_t i = 0U; i < log.count(); ++i) {
        log.format(log.entry(i), text, sizeof(text));
        send(text, sent);
    }
    return CommandResult::Ok;
}

void sendErrorLogSummary()
{
    LogService::instance().errorLogSnapshot(snapshot);
    const ErrorLog::Log log(snapshot);
    const ErrorLog::Entry* newest = log.newest();
    if (newest == nullptr) {
        return;
    }
    char entry[128];
    log.format(*newest, entry, sizeof(entry));
    char text[200];
    std::snprintf(text, sizeof(text), "%u warnings/errors kept ('errors' lists them); newest: %s",
                  static_cast<unsigned>(log.count()), entry);
    LogService::instance().sendLine(text);
}

} // namespace Console
