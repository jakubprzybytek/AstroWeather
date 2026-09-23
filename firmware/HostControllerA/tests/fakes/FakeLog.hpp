#pragma once

// Captures what code under test sends to LogService. Link FakeLogService.cpp
// instead of User/Src/Debug/LogService.cpp; every log(), logf() and sendLine()
// call is recorded synchronously, without the uptime prefix, the queue or USB.

#include <Debug/LogService.hpp>

#include <string>
#include <vector>

namespace FakeLog {

struct Line {
    LogService::Level level;
    std::string text;
};

const std::vector<Line>& lines();
void clear();

// True if any recorded line contains the substring.
bool contains(const std::string& fragment);

// The recorded lines joined with '\n', for failure messages.
std::string dump();

} // namespace FakeLog
