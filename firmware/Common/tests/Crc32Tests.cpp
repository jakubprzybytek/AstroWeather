#include <Utils/Crc32.hpp>

#include <Expect.hpp>

#include <cstdint>
#include <cstring>

using Test::expectEqual;

namespace {

const uint8_t* bytes(const char* text)
{
    return reinterpret_cast<const uint8_t*>(text);
}

uint32_t length(const char* text)
{
    return static_cast<uint32_t>(std::strlen(text));
}

void testCheckValue()
{
    // The catalogue check value of CRC-32/ISO-HDLC (zlib, PNG, Ethernet).
    const char* text = "123456789";
    expectEqual(Crc32::compute(bytes(text), length(text)), 0xCBF43926U, "check value");
}

void testKnownValues()
{
    const char* fox = "The quick brown fox jumps over the lazy dog";
    expectEqual(Crc32::compute(bytes(fox), length(fox)), 0x414FA339U, "quick brown fox");
    const uint8_t zero = 0U;
    expectEqual(Crc32::compute(&zero, 1U), 0xD202EF8DU, "single zero byte");
    const uint8_t ones[4] = {0xFFU, 0xFFU, 0xFFU, 0xFFU};
    expectEqual(Crc32::compute(ones, 4U), 0xFFFFFFFFU, "four 0xFF bytes");
}

void testEmpty()
{
    expectEqual(Crc32::compute(nullptr, 0U), 0U, "empty input");
    expectEqual(Crc32::update(Crc32::kInitial, nullptr, 0U), Crc32::kInitial,
                "empty update leaves the state");
}

void testIncremental()
{
    // As the WiFi task does it: one update per received chunk, finished once.
    const char* text = "123456789";
    for (uint32_t split = 0U; split <= 9U; ++split) {
        uint32_t crc = Crc32::kInitial;
        crc = Crc32::update(crc, bytes(text), split);
        crc = Crc32::update(crc, bytes(text) + split, 9U - split);
        expectEqual(Crc32::finish(crc), 0xCBF43926U, "check value in two pieces");
    }
    uint32_t crc = Crc32::kInitial;
    for (uint32_t index = 0U; index < 9U; ++index) {
        crc = Crc32::update(crc, bytes(text) + index, 1U);
    }
    expectEqual(Crc32::finish(crc), 0xCBF43926U, "check value byte by byte");
}

void testDetectsChange()
{
    char text[] = "123456789";
    text[4] = '6';
    expectEqual(Crc32::compute(bytes(text), length(text)) != 0xCBF43926U, true,
                "one changed byte changes the CRC");
}

} // namespace

int main()
{
    testCheckValue();
    testKnownValues();
    testEmpty();
    testIncremental();
    testDetectsChange();
    return Test::finish("Crc32");
}
