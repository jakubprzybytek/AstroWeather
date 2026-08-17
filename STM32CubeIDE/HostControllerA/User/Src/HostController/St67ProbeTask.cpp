#include <St67ProbeTask.hpp>

#include <Debug/DebugService.hpp>
#include <Utils/Task.hpp>

#include "main.h"

#include <cstring>

extern "C" {
extern SPI_HandleTypeDef hspi1;
}

namespace {

constexpr uint16_t kSpiHeaderSize = 8U;
constexpr uint16_t kMaxProbePayload = 80U;
constexpr uint32_t kTransferTimeoutMs = 500U;

uint16_t alignToWord(uint16_t length) {
  return static_cast<uint16_t>((length + 3U) & ~3U);
}

bool waitForRdy(GPIO_PinState state, uint32_t timeoutMs) {
  const uint32_t startTick = HAL_GetTick();
  while (HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) != state) {
    if ((HAL_GetTick() - startTick) >= timeoutMs) {
      return false;
    }
    osDelay(1);
  }
  return true;
}

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

  bool transferFrame(const uint8_t* txPayload,
                     uint16_t txLength,
                     uint8_t* rxPayload,
                     uint16_t rxCapacity,
                     uint16_t& rxLength,
                     uint32_t readyTimeoutMs) {
    uint8_t txFrame[kSpiHeaderSize + kMaxProbePayload] = {0};
    uint8_t rxFrame[kSpiHeaderSize + kMaxProbePayload] = {0};
    const uint16_t paddedTxLength = alignToWord(txLength);
    const uint16_t firstTransferLength = static_cast<uint16_t>(kSpiHeaderSize + paddedTxLength);

    if (txLength > kMaxProbePayload || firstTransferLength > sizeof(txFrame)) {
      return false;
    }

    txFrame[0] = 0xAA;
    txFrame[1] = 0x55;
    txFrame[2] = static_cast<uint8_t>(txLength & 0xFFU);
    txFrame[3] = static_cast<uint8_t>(txLength >> 8U);
    txFrame[4] = 0x00;
    txFrame[5] = 0x00;
    txFrame[6] = 0x00;
    txFrame[7] = 0x00;
    if (txLength != 0U) {
      std::memcpy(txFrame + kSpiHeaderSize, txPayload, txLength);
    }

    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);
    if (!waitForRdy(GPIO_PIN_SET, readyTimeoutMs)) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 SPI RDY assert timeout");
      return false;
    }

    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1,
                                                       txFrame,
                                                       rxFrame,
                                                       firstTransferLength,
                                                       kTransferTimeoutMs);
    if (status != HAL_OK) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 SPI frame transfer failed: status=%d",
                                    static_cast<int>(status));
      return false;
    }

    const uint16_t peerMagic = static_cast<uint16_t>(rxFrame[0]) |
                               static_cast<uint16_t>(rxFrame[1] << 8U);
    const uint16_t peerLength = static_cast<uint16_t>(rxFrame[2]) |
                                static_cast<uint16_t>(rxFrame[3] << 8U);
    if (peerMagic != 0x55AAU || peerLength > rxCapacity) {
      const GPIO_PinState rdy = HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin);
      (void)waitForRdy(GPIO_PIN_RESET, kTransferTimeoutMs);
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 SPI invalid peer header: %02x %02x %02x %02x %02x %02x %02x %02x, RDY=%u",
                                    rxFrame[0],
                                    rxFrame[1],
                                    rxFrame[2],
                                    rxFrame[3],
                                    rxFrame[4],
                                    rxFrame[5],
                                    rxFrame[6],
                                    rxFrame[7],
                                    (rdy == GPIO_PIN_SET) ? 1U : 0U);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 peer did not drive MISO (magic=0x%04x len=%u)",
                                    peerMagic,
                                    peerLength);
      return false;
    }

    const uint16_t payloadInFirstTransfer =
        (peerLength < paddedTxLength) ? peerLength : paddedTxLength;
    if (payloadInFirstTransfer != 0U) {
      std::memcpy(rxPayload, rxFrame + kSpiHeaderSize, payloadInFirstTransfer);
    }

    if (peerLength > payloadInFirstTransfer) {
      uint8_t txRemainder[kMaxProbePayload] = {0};
      uint8_t rxRemainder[kMaxProbePayload] = {0};
      const uint16_t remainingLength =
          static_cast<uint16_t>(peerLength - payloadInFirstTransfer);
      const uint16_t paddedRemainingLength = alignToWord(remainingLength);
      status = HAL_SPI_TransmitReceive(&hspi1,
                                       txRemainder,
                                       rxRemainder,
                                       paddedRemainingLength,
                                       kTransferTimeoutMs);
      if (status != HAL_OK) {
        HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
        DebugService::instance().logf(DebugService::Level::Error,
                                      "ST67 SPI payload transfer failed: status=%d",
                                      static_cast<int>(status));
        return false;
      }
      std::memcpy(rxPayload + payloadInFirstTransfer, rxRemainder, remainingLength);
    }

    if (!waitForRdy(GPIO_PIN_RESET, kTransferTimeoutMs)) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      DebugService::instance().logf(DebugService::Level::Error,
                                    "ST67 SPI RDY deassert timeout");
      return false;
    }

    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
    rxLength = peerLength;
    return true;
  }

  void probeAtCommand() {
    static const uint8_t kAtCommand[] = "AT\r\n";
    uint8_t response[kMaxProbePayload + 1U] = {0};
    uint16_t responseLen = 0;

    if (!modulePowered_) {
      HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      osDelay(20);

      if (!waitForRdy(GPIO_PIN_RESET, 100U)) {
        DebugService::instance().logf(DebugService::Level::Error,
                                      "ST67 RDY remains high with CHIP_EN=0; check RDY wiring or module power");
        return;
      }

      HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_SET);

      if (!transferFrame(nullptr, 0U, response, kMaxProbePayload, responseLen, 5000U)) {
        return;
      }
      response[responseLen] = '\0';
      DebugService::instance().logf(DebugService::Level::Info,
                                    "ST67 startup: '%s'", response);
      modulePowered_ = true;
    }

    responseLen = 0;
    if (!transferFrame(kAtCommand,
                       sizeof(kAtCommand) - 1U,
                       response,
                       kMaxProbePayload,
                       responseLen,
                       kTransferTimeoutMs)) {
      return;
    }

    if (responseLen == 0U) {
      if (!transferFrame(nullptr,
                         0U,
                         response,
                         kMaxProbePayload,
                         responseLen,
                         kTransferTimeoutMs)) {
        return;
      }
    }

    response[responseLen] = '\0';
    if (std::strstr(reinterpret_cast<const char*>(response), "OK") != nullptr) {
      DebugService::instance().logf(DebugService::Level::Info,
                                    "ST67 SPI AT probe OK: '%s'", response);
    } else {
      DebugService::instance().logf(DebugService::Level::Warn,
                                    "ST67 SPI AT probe no OK, rx='%s'", response);
    }
  }

  bool modulePowered_ = false;
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
