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
        storage_.count > kCapacity) {
        return false;
    }
    // order must list each used slot, 0 .. count - 1, exactly once.
    uint32_t seen = 0U;
    for (uint8_t i = 0U; i < storage_.count; ++i) {
        const uint8_t slot = storage_.order[i];
        if (slot >= storage_.count || (seen & (1UL << slot)) != 0U) {
            return false;
        }
        seen |= 1UL << slot;
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
    return storage_.entries[storage_.order[index]];
}

const Entry* Log::newest() const
{
    return storage_.count == 0U ? nullptr
                                : &entry(static_cast<uint8_t>(storage_.count - 1U));
}

void Log::record(Level level, const char* text, uint16_t hash, Stamp stamp)
{
    const uint8_t levelValue = static_cast<uint8_t>(level);
    // Latest first: a repeat is most likely recent.
    for (uint8_t i = storage_.count; i > 0U; --i) {
        Entry& seen = storage_.entries[storage_.order[i - 1U]];
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
        moveToEnd(static_cast<uint8_t>(i - 1U));
        return;
    }

    uint8_t slot;
    if (storage_.count < kCapacity) {
        slot = storage_.count;
        storage_.order[storage_.count] = slot;
        ++storage_.count;
    } else {
        // Reuse the slot of the entry quiet longest, moved to the end.
        slot = storage_.order[0];
        moveToEnd(0U);
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

void Log::moveToEnd(uint8_t index)
{
    const uint8_t slot = storage_.order[index];
    for (uint8_t i = index; i + 1U < storage_.count; ++i) {
        storage_.order[i] = storage_.order[i + 1U];
    }
    storage_.order[storage_.count - 1U] = slot;
}

void Log::formatStampOf(const Entry& e, bool wall, uint32_t seconds, char* out,
                        std::size_t size) const
{
    char stamp[24];
    formatStamp(wall, seconds, stamp, sizeof(stamp));
    // Uptime means nothing across a reset without saying which boot.
    const uint32_t ago = storage_.boot - e.boot;
    if (wall || ago == 0U) {
        std::snprintf(out, size, "%s", stamp);
    } else if (ago == 1U) {
        std::snprintf(out, size, "%s (previous boot)", stamp);
    } else {
        std::snprintf(out, size, "%s (%lu boots ago)", stamp, static_cast<unsigned long>(ago));
    }
}

int Log::format(const Entry& e, char* out, std::size_t size) const
{
    // Led by the latest occurrence, the order the list is in; a repeat adds
    // its count and when it first happened.
    char latest[48];
    formatStampOf(e, (e.flags & kLastWall) != 0U, e.last, latest, sizeof(latest));

    char repeat[64] = "";
    if (e.count > 1U) {
        char first[48];
        formatStampOf(e, (e.flags & kFirstWall) != 0U, e.first, first, sizeof(first));
        std::snprintf(repeat, sizeof(repeat), " %lux, first %s", static_cast<unsigned long>(e.count),
                      first);
    }

    const char levelLetter = e.level == static_cast<uint8_t>(Level::Error) ? 'E' : 'W';
    return std::snprintf(out, size, "%c %s%s: %s", levelLetter, latest, repeat, e.text);
}

} // namespace ErrorLog
