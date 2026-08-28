#include <Device/SCT2xxx.hpp>

SCT2xxx::SCT2xxx(SPI_HandleTypeDef* spi,
                 GPIO_TypeDef* enablePort,
                 uint16_t enablePin,
                 GPIO_TypeDef* latchPort,
                 uint16_t latchPin)
    : spi_(spi),
      enablePort_(enablePort),
      enablePin_(enablePin),
      latchPort_(latchPort),
      latchPin_(latchPin) {}

HAL_StatusTypeDef SCT2xxx::send(uint8_t value)
{
    HAL_StatusTypeDef status = HAL_SPI_Transmit(spi_, &value, 1, HAL_MAX_DELAY);
    if (status != HAL_OK) {
        return status;
    }

    HAL_GPIO_WritePin(latchPort_, latchPin_, GPIO_PIN_SET);
    HAL_GPIO_WritePin(latchPort_, latchPin_, GPIO_PIN_RESET);
    return HAL_OK;
}

void SCT2xxx::enable()
{
    HAL_GPIO_WritePin(enablePort_, enablePin_, GPIO_PIN_RESET);
}

void SCT2xxx::disable()
{
    HAL_GPIO_WritePin(enablePort_, enablePin_, GPIO_PIN_SET);
}