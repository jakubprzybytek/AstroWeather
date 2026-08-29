#include <Display/DisplayAddress.hpp>

namespace Display {
namespace {

uint8_t readPin(GPIO_TypeDef* port, uint16_t pin)
{
    GPIO_InitTypeDef init{};
    init.Pin = pin;
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(port, &init);
    if (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) {
        return 2U;
    }
    init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(port, &init);
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET ? 1U : 0U;
}

} // namespace

uint8_t detectBoardId(GPIO_TypeDef* port0, uint16_t pin0,
                      GPIO_TypeDef* port1, uint16_t pin1,
                      GPIO_TypeDef* port2, uint16_t pin2)
{
    const uint8_t state0 = readPin(port0, pin0);
    const uint8_t state1 = readPin(port1, pin1);
    const uint8_t state2 = readPin(port2, pin2);
    GPIO_InitTypeDef restore{};
    restore.Mode = GPIO_MODE_INPUT;
    restore.Pull = GPIO_NOPULL;
    restore.Pin = pin0;
    HAL_GPIO_Init(port0, &restore);
    restore.Pin = pin1;
    HAL_GPIO_Init(port1, &restore);
    restore.Pin = pin2;
    HAL_GPIO_Init(port2, &restore);
    return static_cast<uint8_t>(state0 + 3U * state1 + 9U * state2);
}

uint16_t boardAddress(uint8_t boardId)
{
    return boardId < 27U ? static_cast<uint16_t>(0x10U + boardId) : 0U;
}

uint16_t detectBoardAddress()
{
    return boardAddress(detectBoardId(ADDR_0_GPIO_Port, ADDR_0_Pin,
                                      ADDR_1_GPIO_Port, ADDR_1_Pin,
                                      ADDR_2_GPIO_Port, ADDR_2_Pin));
}

} // namespace Display
