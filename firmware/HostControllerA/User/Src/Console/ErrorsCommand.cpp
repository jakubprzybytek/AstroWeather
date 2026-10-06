#include <Console/ErrorsCommand.hpp>

#include <Console/PacedOutput.hpp>
#include <Debug/LogService.hpp>

#include <cstdio>
#include <cstring>

namespace Console {
namespace {

// A consistent copy to read while other tasks keep logging, and the line
// being formatted. Static, not on the console task's stack: the copy is over
// 4 KB and a line up to a log line long. Console task only.
ErrorLog::Storage snapshot;
char text[256];
char entryText[256];

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
    PacedOutput out;
    std::snprintf(text, sizeof(text), "OK errors %u of %u kept, %lu older dropped, boot %lu",
                  static_cast<unsigned>(log.count()), static_cast<unsigned>(ErrorLog::kCapacity),
                  static_cast<unsigned long>(log.dropped()), static_cast<unsigned long>(log.boot()));
    out.line(text);
    for (uint8_t i = 0U; i < log.count(); ++i) {
        log.format(log.entry(i), text, sizeof(text));
        out.line(text);
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
    log.format(*newest, entryText, sizeof(entryText));
    std::snprintf(text, sizeof(text), "%u warnings/errors kept ('errors' lists them); newest: %.196s",
                  static_cast<unsigned>(log.count()), entryText);
    LogService::instance().sendLine(text);
}

} // namespace Console
