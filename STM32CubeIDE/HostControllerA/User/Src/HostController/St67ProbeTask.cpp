#include <St67ProbeTask.hpp>

#include <Debug/DebugService.hpp>
#include <Utils/Task.hpp>

#include "main.h"

#include <cstring>

extern "C" {
extern UART_HandleTypeDef huart2;
}

namespace {

class St67ProbeTask : public Task<2048> {
 public:
  static St67ProbeTask& instance() {
    static St67ProbeTask task;
    return task;
  }

  void trigger() {
    osThreadFlagsSet(getHandle(), kFlagTrigger);
  }

 protected:
  void run() override {
    // Wait until scheduler and USB debug path are up.
    osDelay(4000);

    for (;;) {
      uint32_t flags = osThreadFlagsWait(kFlagTrigger, osFlagsWaitAny, osWaitForever);
      if ((flags & osFlagsError) != 0U) {
        continue;
      }
      probeAtCommand();
    }
  }

 private:
  static constexpr uint32_t kFlagTrigger = 1u << 0;

  St67ProbeTask() : Task<2048>("St67Probe", osPriorityBelowNormal) {}

  void probeAtCommand() {
    static const uint8_t kAtCommand[] = "AT\r\n";
    uint8_t response[80] = {0};
    uint16_t responseLen = 0;
    bool seenAnyByte = false;

    // For UART AT probing: keep SPI CS deasserted and BOOT in normal mode.
    HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    osDelay(20);

    GPIO_PinState rdy = HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin);
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 pre-probe pins: CS=1 BOOT=0 RDY=%d", (rdy == GPIO_PIN_SET) ? 1 : 0);

    // Drain any stale bytes left in RX before the probe.
    for (;;) {
      uint8_t discard = 0;
      if (HAL_UART_Receive(&huart2, &discard, 1, 2) != HAL_OK) {
        break;
      }
    }

    HAL_StatusTypeDef tx = HAL_UART_Transmit(&huart2,
                                             const_cast<uint8_t*>(kAtCommand),
                                             sizeof(kAtCommand) - 1,
                                             100);
    if (tx != HAL_OK) {
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 probe TX failed: status=%d", static_cast<int>(tx));
      return;
    }

    uint32_t startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < 500U && responseLen < (sizeof(response) - 1U)) {
      uint8_t byte = 0;
      HAL_StatusTypeDef rx = HAL_UART_Receive(&huart2, &byte, 1, 30);
      if (rx == HAL_OK) {
        response[responseLen++] = byte;
        seenAnyByte = true;
        if (responseLen >= 2U && response[responseLen - 2U] == 'O' && response[responseLen - 1U] == 'K') {
          break;
        }
      } else if (rx == HAL_TIMEOUT) {
        if (seenAnyByte) {
          break;
        }
      } else {
        DebugService::instance().logf(DebugService::Level::Error,
                                      "ST67 probe RX failed: status=%d", static_cast<int>(rx));
        return;
      }
    }

    response[responseLen] = '\0';
    if (std::strstr(reinterpret_cast<const char*>(response), "OK") != nullptr) {
      DebugService::instance().logf(DebugService::Level::Info,
                                    "ST67 AT probe OK: '%s'", response);
    } else {
      DebugService::instance().logf(DebugService::Level::Warn,
                                    "ST67 AT probe no OK, rx='%s'", response);
    }
  }
};

}  // namespace

namespace HostController {

void StartSt67ProbeTask() {
  St67ProbeTask::instance().start();
}

void TriggerSt67Probe() {
  St67ProbeTask::instance().trigger();
}

}  // namespace HostController
