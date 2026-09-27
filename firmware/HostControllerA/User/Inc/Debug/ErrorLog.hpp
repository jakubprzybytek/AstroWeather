#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The last warnings and errors logged, kept for 'errors' and the console's
// welcome message. Pure: LogService feeds it and supplies the storage, which
// lives in a RAM region the startup code does not clear, so the log survives
// a reset, a crash and reflashing, but not a power loss. See
// docs/Console.md#error-log.
//
// A message identical to an entry of the current boot, at the same level,
// only bumps that entry's count and last time; the entry keeps its place and
// first time. So an error repeated every retry or every sample, or a set of
// them repeated every refresh, takes one entry each. When all kCapacity
// entries are used the oldest is overwritten and counted as dropped.
namespace ErrorLog {

constexpr uint8_t kCapacity = 16U;
constexpr std::size_t kTextSize = 102U;  // message text, NUL included

enum class Level : uint8_t { Warning = 1U, Error = 2U };

// When something was logged: local time in seconds since 2000-01-01 when the
// clock was set (Calendar::secondsSince2000()), else seconds of uptime.
struct Stamp
{
    bool wall;
    uint32_t seconds;
};

struct Entry
{
    uint32_t first;  // Stamp::seconds of the first occurrence
    uint32_t last;   // and of the latest
    uint32_t count;  // occurrences, 1 or more
    uint32_t boot;   // Storage::boot when it was logged
    uint16_t hash;   // textHash() of text, to find repeats quickly
    uint8_t level;   // Level
    uint8_t flags;   // kFirstWall, kLastWall
    char text[kTextSize];
};

constexpr uint8_t kFirstWall = 1U << 0U;
constexpr uint8_t kLastWall = 1U << 1U;

// The whole log, as kept in the retained RAM region. Its layout is part of
// what survives a reflash: a change must change kVersion, and an unknown
// version starts the log afresh.
struct Storage
{
    uint32_t magic;
    uint32_t check;    // ~magic ^ kVersion ^ sizeof(Storage)
    uint32_t boot;     // boots seen, counting this one
    uint32_t dropped;  // entries overwritten since the log was last cleared
    uint8_t head;      // index of the oldest entry
    uint8_t count;     // entries in use
    uint8_t reserved[2];
    std::array<Entry, kCapacity> entries;
};

constexpr uint32_t kMagic = 0x4552524CU;  // "ERRL"
constexpr uint32_t kVersion = 2U;

// A 16-bit hash of the text as it would be stored (truncated to
// kTextSize - 1). Cheap to compare; the text is compared only on a match.
uint16_t textHash(const char* text);

class Log
{
public:
    explicit Log(Storage& storage) : storage_(storage) {}

    // Once per boot, before the first record(): keeps what survived if it is
    // consistent, else starts empty, and counts the boot.
    void begin();

    // Not thread-safe: the caller serializes, LogService with a critical
    // section. `text` is truncated to kTextSize - 1 characters; `hash` is
    // textHash(text), computed by the caller outside that section.
    void record(Level level, const char* text, uint16_t hash, Stamp stamp);
    void record(Level level, const char* text, Stamp stamp)
    {
        record(level, text, textHash(text), stamp);
    }
    void clear();

    uint8_t count() const { return storage_.count; }
    uint32_t dropped() const { return storage_.dropped; }
    uint32_t boot() const { return storage_.boot; }
    // index 0 is the oldest.
    const Entry& entry(uint8_t index) const;
    const Entry* newest() const;

    // One entry as a line: "E 2026-09-27 00:26:54 <text>", with the repeat
    // count and last time when it repeated, and uptime stamps from an earlier
    // boot marked as such. Returns the length written, like snprintf.
    int format(const Entry& entry, char* out, std::size_t size) const;

private:
    bool consistent() const;
    void reset(uint32_t boot);

    Storage& storage_;
};

// "2026-09-27 00:26:54" or "up 0d 00:00:42".
void formatStamp(bool wall, uint32_t seconds, char* out, std::size_t size);

} // namespace ErrorLog
