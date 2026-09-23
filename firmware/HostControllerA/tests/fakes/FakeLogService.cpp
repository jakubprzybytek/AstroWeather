#include "FakeLog.hpp"

#include <cstdarg>
#include <cstdio>

namespace {

std::vector<FakeLog::Line> recorded;

// The real LogService stores whole records of this size, prefix included, so
// long messages are cut short on the device; the fake keeps the same limit on
// the message itself.
constexpr std::size_t kMaxMessage = 200U;

bool record(LogService::Level level, const char* text)
{
    recorded.push_back({level, text != nullptr ? text : ""});
    return true;
}

} // namespace

namespace FakeLog {

const std::vector<Line>& lines()
{
    return recorded;
}

void clear()
{
    recorded.clear();
}

bool contains(const std::string& fragment)
{
    for (const Line& line : recorded) {
        if (line.text.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string dump()
{
    std::string text;
    for (const Line& line : recorded) {
        text += line.text;
        text += '\n';
    }
    return text;
}

} // namespace FakeLog

LogService& LogService::instance()
{
    static LogService service;
    return service;
}

LogService::LogService()
    : Task<1536>("LogService", osPriorityNormal),
      logQueueHandle_(nullptr), logQueueCb_{}, logQueueStorage_{}, txBuffer_{},
      sentCount_(0), droppedCount_(0), busyDropCount_(0), statsEnabled_(false)
{
}

void LogService::init()
{
}

bool LogService::log(Level level, const char* message)
{
    return record(level, message);
}

bool LogService::logf(Level level, const char* format, ...)
{
    char text[kMaxMessage];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return record(level, text);
}

bool LogService::sendLine(const char* message)
{
    return record(Level::Info, message);
}

void LogService::setStatsEnabled(bool enabled)
{
    statsEnabled_ = enabled;
}

void LogService::run()
{
}
