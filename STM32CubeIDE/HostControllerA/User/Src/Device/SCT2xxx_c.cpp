#include <Device/SCT2xxx.hpp>
#include <Device/SCT2xxx_c.h>

extern SPI_HandleTypeDef hspi3;

static SCT2xxx sct2xxx(
    &hspi3,
    SCT_ENABLE_GPIO_Port,
    SCT_ENABLE_Pin,
    SCT_LATCH_GPIO_Port,
    SCT_LATCH_Pin);

extern "C" HAL_StatusTypeDef SCT2xxx_Send(uint8_t value)
{
    return sct2xxx.send(value);
}

extern "C" HAL_StatusTypeDef SCT2xxx_SendBuffer(const uint8_t* data, uint16_t size)
{
    return sct2xxx.send(data, size);
}

extern "C" void SCT2xxx_Enable(void)
{
    sct2xxx.enable();
}

extern "C" void SCT2xxx_Disable(void)
{
    sct2xxx.disable();
}