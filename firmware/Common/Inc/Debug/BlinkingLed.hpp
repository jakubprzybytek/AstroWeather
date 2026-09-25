#include <Utils/Task.hpp>
#include "main.h"  // GPIO_TypeDef, HAL_GPIO_* types

// Blinks an active-high LED: on for onMs, then off for offMs, repeatedly.
class BlinkingLed : public Task<768>
{
public:
    BlinkingLed(GPIO_TypeDef* port, uint16_t pin, uint32_t onMs, uint32_t offMs,
                const char* name = "BlinkingLed");

    void init();

protected:
    void run() override;

private:
    GPIO_TypeDef* port_;
    uint16_t pin_;
    uint32_t onMs_;
    uint32_t offMs_;
};