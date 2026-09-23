// Checks the native test infrastructure itself: the HAL/RTOS stubs and the
// recording LogService fake.

#include <Expect.hpp>
#include <FakeLog.hpp>
#include <StubHal.hpp>

#include "cmsis_os2.h"
#include "main.h"

namespace {

using Test::expect;
using Test::expectEqual;

void testFakeClock()
{
    Stub::reset();
    expectEqual(HAL_GetTick(), 0U, "clock starts at zero");
    Stub::setTick(1000U);
    osDelay(250U);
    HAL_Delay(50U);
    expectEqual(HAL_GetTick(), 1300U, "delays advance the clock");
    expectEqual(osKernelGetTickCount(), 1300U, "kernel tick follows the clock");
}

void testGpioStraps()
{
    Stub::reset();
    GPIO_InitTypeDef init{};
    init.Pin = ADDR_0_Pin;
    init.Mode = GPIO_MODE_INPUT;

    init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ADDR_0_GPIO_Port, &init);
    expectEqual(HAL_GPIO_ReadPin(ADDR_0_GPIO_Port, ADDR_0_Pin), GPIO_PIN_SET,
                "floating pin follows the pull-up");

    Stub::setStrap(ADDR_0_GPIO_Port, ADDR_0_Pin, Stub::Strap::Low);
    expectEqual(HAL_GPIO_ReadPin(ADDR_0_GPIO_Port, ADDR_0_Pin), GPIO_PIN_RESET,
                "strap overrides the pull");

    HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, GPIO_PIN_SET);
    expectEqual(Stub::outputState(LED_2_GPIO_Port, LED_2_Pin), GPIO_PIN_SET,
                "output latch records writes");
}

void testFakeLogRecordsLines()
{
    FakeLog::clear();
    LogService& log = LogService::instance();
    log.logf(LogService::Level::Warn, "value=%d", 42);
    log.sendLine("OK done");

    expectEqual(FakeLog::lines().size(), static_cast<std::size_t>(2U), "two lines recorded");
    expect(FakeLog::lines()[0].level == LogService::Level::Warn, "level kept");
    expectEqual(FakeLog::lines()[0].text, std::string("value=42"), "logf formats");
    expect(FakeLog::contains("OK done"), "sendLine recorded");
    FakeLog::clear();
    expect(FakeLog::lines().empty(), "clear empties the log");
}

} // namespace

int main()
{
    testFakeClock();
    testGpioStraps();
    testFakeLogRecordsLines();
    return Test::finish("test support");
}
