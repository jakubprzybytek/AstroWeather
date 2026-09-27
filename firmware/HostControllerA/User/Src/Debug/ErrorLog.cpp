#include <Debug/ErrorLog.hpp>

#include <Clock/CalendarDate.hpp>

#include <cstdio>
#include <cstring>

namespace ErrorLog {
namespace {

constexpr uint32_t expectedCheck()
{
    return ~kMagic ^ kVersion ^ static_cast<uint32_t>(sizeof(Storage));
}

bool terminated(const char (&text)[kTextSize])
{
    return std::memchr(text, '\0', kTextSize) != nullptr;
}

} // namespace

uint16_t textHash(const char* text)
{
    // FNV-1a, folded to 16 bits.
    uint32_t hash = 2166136261U;
    for (std::size_t i = 0U; i < kTextSize - 1U && text[i] != '\0'; ++i) {
        hash = (hash ^ static_cast<uint8_t>(text[i])) * 16777619U;
    }
    return static_cast<uint16_t>(hash ^ (hash >> 16U));
}

void formatStamp(bool wall, uint32_t seconds, char* out, std::size_t size)
{
    if (wall) {
        const Calendar::DateTime t = Calendar::fromSecondsSince2000(seconds);
        std::snprintf(out, size, "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(t.year),
                      static_cast<unsigned>(t.month), static_cast<unsigned>(t.day),
                      static_cast<unsigned>(t.hour), static_cast<unsigned>(t.minute),
                      static_cast<unsigned>(t.second));
        return;
    }
    std::snprintf(out, size, "up %lud %02lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 86400U),
                  static_cast<unsigned long>((seconds / 3600U) % 24U),
                  static_cast<unsigned long>((seconds / 60U) % 60U),
                  static_cast<unsigned long>(seconds % 60U));
}

bool Log::consistent() const
{
    if (storage_.magic != kMagic || storage_.check != expectedCheck() ||
        storage_.head >= kCapacity || storage_.count > kCapacity) {
        return false;
    }
    for (uint8_t i = 0U; i < storage_.count; ++i) {
        const Entry& e = entry(i);
        if ((e.level != static_cast<uint8_t>(Level::Warning) &&
             e.level != static_cast<uint8_t>(Level::Error)) ||
            e.count == 0U || !terminated(e.text)) {
            return false;
        }
    }
    return true;
}

void Log::reset(uint32_t boot)
{
    std::memset(&storage_, 0, sizeof(storage_));
    storage_.magic = kMagic;
    storage_.check = expectedCheck();
    storage_.boot = boot;
}

void Log::begin()
{
    if (!consistent()) {
        reset(0U);
    }
    ++storage_.boot;
}

void Log::clear()
{
    reset(storage_.boot);
}

const Entry& Log::entry(uint8_t index) const
{
    return storage_.entries[(storage_.head + index) % kCapacity];
}

const Entry* Log::newest() const
{
    return storage_.count == 0U ? nullptr
                                : &entry(static_cast<uint8_t>(storage_.count - 1U));
}

void Log::record(Level level, const char* text, uint16_t hash, Stamp stamp)
{
    const uint8_t levelValue = static_cast<uint8_t>(level);
    // Newest first: a repeat is most likely recent.
    for (uint8_t i = storage_.count; i > 0U; --i) {
        Entry& seen = storage_.entries[(storage_.head + i - 1U) % kCapacity];
        if (seen.hash != hash || seen.level != levelValue || seen.boot != storage_.boot ||
            std::strncmp(seen.text, text, kTextSize - 1U) != 0) {
            continue;
        }
        if (seen.count != UINT32_MAX) {
            ++seen.count;
        }
        seen.last = stamp.seconds;
        seen.flags =
            static_cast<uint8_t>((seen.flags & ~kLastWall) | (stamp.wall ? kLastWall : 0U));
        return;
    }

    uint8_t slot;
    if (storage_.count < kCapacity) {
        slot = static_cast<uint8_t>((storage_.head + storage_.count) % kCapacity);
        ++storage_.count;
    } else {
        slot = storage_.head;
        storage_.head = static_cast<uint8_t>((storage_.head + 1U) % kCapacity);
        if (storage_.dropped != UINT32_MAX) {
            ++storage_.dropped;
        }
    }
    Entry& e = storage_.entries[slot];
    e.first = stamp.seconds;
    e.last = stamp.seconds;
    e.count = 1U;
    e.boot = storage_.boot;
    e.hash = hash;
    e.level = levelValue;
    e.flags = stamp.wall ? static_cast<uint8_t>(kFirstWall | kLastWall) : 0U;
    std::strncpy(e.text, text, kTextSize - 1U);
    e.text[kTextSize - 1U] = '\0';
}

int Log::format(const Entry& e, char* out, std::size_t size) const
{
    char first[24];
    formatStamp((e.flags & kFirstWall) != 0U, e.first, first, sizeof(first));

    // Uptime means nothing across a reset without saying which boot.
    char boot[24] = "";
    if ((e.flags & kFirstWall) == 0U && e.boot != storage_.boot) {
        const uint32_t ago = storage_.boot - e.boot;
        if (ago == 1U) {
            std::snprintf(boot, sizeof(boot), " (previous boot)");
        } else {
            std::snprintf(boot, sizeof(boot), " (%lu boots ago)", static_cast<unsigned long>(ago));
        }
    }

    char repeat[48] = "";
    if (e.count > 1U) {
        char last[24];
        formatStamp((e.flags & kLastWall) != 0U, e.last, last, sizeof(last));
        std::snprintf(repeat, sizeof(repeat), " x%lu, last %s", static_cast<unsigned long>(e.count),
                      last);
    }

    const char levelLetter = e.level == static_cast<uint8_t>(Level::Error) ? 'E' : 'W';
    return std::snprintf(out, size, "%c %s%s%s: %s", levelLetter, first, boot, repeat, e.text);
}

} // namespace ErrorLog
