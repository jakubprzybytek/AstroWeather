#include <Display/BufferedDisplayBoard.hpp>
#include <Display/DisplayI2cProtocol.hpp>

namespace Display {

void BufferedDisplayBoard::submit()
{
    I2cMessage message{};
    serializeI2c(state_, message);
    if (address_ < 0x10U || address_ > 0x2AU) {
        lastStatus_ = HAL_ERROR;
        return;
    }
    lastStatus_ = HAL_I2C_Master_Transmit(&bus_, static_cast<uint16_t>(address_ << 1U),
                                          message.data(), static_cast<uint16_t>(message.size()),
                                          HAL_MAX_DELAY);
}

} // namespace Display
