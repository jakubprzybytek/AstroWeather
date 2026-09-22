#include <Display/DisplayCodec.hpp>

#include <cstdlib>
#include <iostream>

namespace {

void expectEqual(uint32_t actual, uint32_t expected, const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed: expected " << expected
                  << ", got " << actual << '\n';
        std::exit(EXIT_FAILURE);
    }
}

uint32_t matrixValue(const Display::PreparedFrame& frame, uint8_t slot)
{
    const uint8_t* bytes = &frame[slot * Display::kBytesPerSlot];
    return (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 8U) |
           static_cast<uint32_t>(bytes[4]);
}

void testMatrixRowsMapTopToBottom()
{
    Display::LogicalBoardState state{};
    state.matrix = {0x00001U, 0x00012U, 0x00123U, 0x01234U, 0x12345U};
    Display::PreparedFrame frame{};

    Display::encodePcb(state, frame);

    expectEqual(matrixValue(frame, 0U), state.matrix[4], "physical bottom row");
    expectEqual(matrixValue(frame, 1U), state.matrix[3], "physical row 1");
    expectEqual(matrixValue(frame, 2U), state.matrix[2], "physical row 2");
    expectEqual(matrixValue(frame, 3U), state.matrix[1], "physical row 3");
    expectEqual(matrixValue(frame, 4U), state.matrix[0], "physical top row");
}

} // namespace

int main()
{
    testMatrixRowsMapTopToBottom();
    std::cout << "DisplayCodec tests passed\n";
    return EXIT_SUCCESS;
}