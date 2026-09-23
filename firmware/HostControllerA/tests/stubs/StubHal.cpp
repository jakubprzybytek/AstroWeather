#include "StubHal.hpp"

#include <array>

GPIO_TypeDef stubGpioPorts[4] = {{0U}, {1U}, {2U}, {3U}};

namespace {

constexpr std::size_t kPorts = 4U;
constexpr std::size_t kPins = 16U;

struct PinModel {
    Stub::Strap strap = Stub::Strap::Floating;
    uint32_t mode = GPIO_MODE_INPUT;
    uint32_t pull = GPIO_NOPULL;
    GPIO_PinState output = GPIO_PIN_RESET;
};

uint32_t tickMs = 0U;
std::array<std::array<PinModel, kPins>, kPorts> pins{};

// Calls fn for every pin set in the mask, so multi-pin masks work as on the HAL.
template <typename Fn>
void forEachPin(GPIO_TypeDef* port, uint16_t mask, Fn fn)
{
    if (port == nullptr || port->index >= kPorts) {
        return;
    }
    for (std::size_t bit = 0U; bit < kPins; ++bit) {
        if ((mask & (1U << bit)) != 0U) {
            fn(pins[port->index][bit]);
        }
    }
}

PinModel* firstPin(GPIO_TypeDef* port, uint16_t mask)
{
    PinModel* found = nullptr;
    forEachPin(port, mask, [&found](PinModel& pin) {
        if (found == nullptr) {
            found = &pin;
        }
    });
    return found;
}

} // namespace

namespace Stub {

void reset()
{
    tickMs = 0U;
    pins = {};
}

void setTick(uint32_t value)
{
    tickMs = value;
}

void advanceTick(uint32_t deltaMs)
{
    tickMs += deltaMs;
}

uint32_t tick()
{
    return tickMs;
}

void setStrap(GPIO_TypeDef* port, uint16_t pin, Strap strap)
{
    forEachPin(port, pin, [strap](PinModel& model) { model.strap = strap; });
}

GPIO_PinState outputState(GPIO_TypeDef* port, uint16_t pin)
{
    const PinModel* model = firstPin(port, pin);
    return model != nullptr ? model->output : GPIO_PIN_RESET;
}

uint32_t configuredPull(GPIO_TypeDef* port, uint16_t pin)
{
    const PinModel* model = firstPin(port, pin);
    return model != nullptr ? model->pull : GPIO_NOPULL;
}

} // namespace Stub

extern "C" {

void HAL_GPIO_Init(GPIO_TypeDef* port, GPIO_InitTypeDef* init)
{
    if (init == nullptr) {
        return;
    }
    forEachPin(port, static_cast<uint16_t>(init->Pin), [init](PinModel& model) {
        model.mode = init->Mode;
        model.pull = init->Pull;
    });
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* port, uint16_t pin)
{
    const PinModel* model = firstPin(port, pin);
    if (model == nullptr) {
        return GPIO_PIN_RESET;
    }
    if (model->mode == GPIO_MODE_OUTPUT_PP) {
        return model->output;
    }
    switch (model->strap) {
    case Stub::Strap::High:
        return GPIO_PIN_SET;
    case Stub::Strap::Low:
        return GPIO_PIN_RESET;
    case Stub::Strap::Floating:
    default:
        return model->pull == GPIO_PULLUP ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
}

void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    forEachPin(port, pin, [state](PinModel& model) { model.output = state; });
}

uint32_t HAL_GetTick(void)
{
    return tickMs;
}

void HAL_Delay(uint32_t delayMs)
{
    tickMs += delayMs;
}

} // extern "C"
