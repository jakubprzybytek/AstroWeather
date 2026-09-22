#include <Utils/Task.hpp>
#include "main.h"  // GPIO_TypeDef, HAL_GPIO_* types

class BlinkingLed : public Task<768>
{
public:
    BlinkingLed(GPIO_TypeDef* port, uint16_t pin, uint32_t periodMs,
                const char* name = "BlinkingLed");

    void init();

protected:
    void run() override;

private:
    GPIO_TypeDef* port_;
    uint16_t pin_;
    uint32_t periodMs_;
};