#include <St67ProbeTask.hpp>

#include <Debug/DebugService.hpp>
#include <Utils/Task.hpp>

#include "main.h"

#include <cstring>

extern "C" {
extern SPI_HandleTypeDef hspi1;
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

    // Chip powered, normal (non-boot) mode, CS idle deasserted before selecting it.
    HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);
    osDelay(20);

    GPIO_PinState rdy = HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin);
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 pre-probe pins: CS=1 BOOT=0 RDY=%d", (rdy == GPIO_PIN_SET) ? 1 : 0);

    // Select the slave for the AT command transaction.
    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef tx = HAL_SPI_Transmit(&hspi1,
                                            const_cast<uint8_t*>(kAtCommand),
                                            sizeof(kAtCommand) - 1,
                                            100);
    if (tx != HAL_OK) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 SPI probe TX failed: status=%d", static_cast<int>(tx));
      return;
    }

    // RDY signals a response byte is ready to be clocked in; it drops once the slave is drained.
    uint32_t startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < 500U && responseLen < (sizeof(response) - 1U)) {
      if (HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) != GPIO_PIN_SET) {
        if (seenAnyByte) {
          break;
        }
        osDelay(1);
        continue;
      }

      uint8_t byte = 0;
      HAL_StatusTypeDef rx = HAL_SPI_Receive(&hspi1, &byte, 1, 30);
      if (rx != HAL_OK) {
        DebugService::instance().logf(DebugService::Level::Error,
                                      "ST67 SPI probe RX failed: status=%d", static_cast<int>(rx));
        HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);
        return;
      }
      response[responseLen++] = byte;
      seenAnyByte = true;
      if (responseLen >= 2U && response[responseLen - 2U] == 'O' && response[responseLen - 1U] == 'K') {
        break;
      }
    }

    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);

    response[responseLen] = '\0';
    if (std::strstr(reinterpret_cast<const char*>(response), "OK") != nullptr) {
      DebugService::instance().logf(DebugService::Level::Info,
                                    "ST67 SPI AT probe OK: '%s'", response);
    } else {
      DebugService::instance().logf(DebugService::Level::Warn,
                                    "ST67 SPI AT probe no OK, rx='%s'", response);
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
