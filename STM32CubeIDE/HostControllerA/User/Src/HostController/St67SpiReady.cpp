#include "main.h"
#include "spi_iface.h"

extern "C" void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ST67_RDY_Pin)
    {
        (void)spi_on_txn_data_ready();
    }
}
