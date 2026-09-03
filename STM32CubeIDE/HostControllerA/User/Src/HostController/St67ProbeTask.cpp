#include <St67ProbeTask.hpp>

#include <Debug/LogService.hpp>
#include <Utils/Task.hpp>

#include "main.h"

#include <algorithm>
#include <cstring>

extern "C" {
extern SPI_HandleTypeDef hspi1;
}

namespace {

constexpr uint16_t kSpiHeaderSize = 8U;
constexpr uint16_t kMaxProbePayload = 80U;
constexpr uint32_t kTransferTimeoutMs = 500U;
constexpr uint32_t kReadyReadRetries = 5U;
constexpr uint32_t kAtCommandRetries = 5U;
constexpr uint32_t kCwlapReadyTimeoutMs = 3000U;
constexpr uint32_t kCwlapReadAttempts = 60U;
constexpr uint32_t kCwlapScanRetries = 3U;
constexpr size_t kCwlapBufferSize = 4096U;

uint16_t alignToWord(uint16_t length) {
  return static_cast<uint16_t>((length + 3U) & ~3U);
}

uint32_t countOccurrences(const uint8_t* text, const char* needle) {
  const char* haystack = reinterpret_cast<const char*>(text);
  const size_t needleLen = std::strlen(needle);
  uint32_t count = 0U;
  const char* found = std::strstr(haystack, needle);
  while (found != nullptr) {
    ++count;
    found = std::strstr(found + needleLen, needle);
  }
  return count;
}

bool hasAtTerminator(const uint8_t* text) {
  const char* haystack = reinterpret_cast<const char*>(text);
  return std::strstr(haystack, "OK") != nullptr || std::strstr(haystack, "ERROR") != nullptr;
}

bool waitForRdy(GPIO_PinState state, uint32_t timeoutMs, uint32_t& waitedMs) {
  const uint32_t startTick = HAL_GetTick();
  while (HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) != state) {
    waitedMs = HAL_GetTick() - startTick;
    if (waitedMs >= timeoutMs) {
      return false;
    }
    osDelay(1);
  }
  waitedMs = HAL_GetTick() - startTick;
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

  void triggerCwlap() {
    osThreadFlagsSet(getHandle(), kFlagTriggerCwlap);
  }

 protected:
  void run() override {
    // Wait until scheduler and USB debug path are up.
    osDelay(4000);

    for (;;) {
      uint32_t flags = osThreadFlagsWait(kFlagTrigger | kFlagTriggerCwlap, osFlagsWaitAny, osWaitForever);
      if ((flags & osFlagsError) != 0U) {
        continue;
      }
      if ((flags & kFlagTrigger) != 0U) {
        probeAtCommand();
      }
      if ((flags & kFlagTriggerCwlap) != 0U) {
        probeCwlap();
      }
    }
  }

 private:
  static constexpr uint32_t kFlagTrigger = 1u << 0;
  static constexpr uint32_t kFlagTriggerCwlap = 1u << 1;

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
    uint16_t wireTransferLength = firstTransferLength;

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
    uint32_t rdyAssertWaitMs = 0U;
    if (!waitForRdy(GPIO_PIN_SET, readyTimeoutMs, rdyAssertWaitMs)) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      LogService::instance().logf(LogService::Level::Error,
                                    "ST67 SPI RDY assert timeout after %lums",
                                    static_cast<unsigned long>(rdyAssertWaitMs));
      return false;
    }

    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(&hspi1,
                                                       txFrame,
                                                       rxFrame,
                                                       firstTransferLength,
                                                       kTransferTimeoutMs);
    if (status != HAL_OK) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      LogService::instance().logf(LogService::Level::Error,
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
      uint32_t rdyDeassertWaitMs = 0U;
      (void)waitForRdy(GPIO_PIN_RESET, kTransferTimeoutMs, rdyDeassertWaitMs);
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      LogService::instance().logf(LogService::Level::Error,
                                    "ST67 SPI invalid peer header: %02x %02x %02x %02x %02x %02x %02x %02x, RDY=%u, waited=%lums",
                                    rxFrame[0],
                                    rxFrame[1],
                                    rxFrame[2],
                                    rxFrame[3],
                                    rxFrame[4],
                                    rxFrame[5],
                                    rxFrame[6],
                                    rxFrame[7],
                                    (rdy == GPIO_PIN_SET) ? 1U : 0U,
                                    static_cast<unsigned long>(rdyDeassertWaitMs));
      LogService::instance().logf(LogService::Level::Error,
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
        LogService::instance().logf(LogService::Level::Error,
                                      "ST67 SPI payload transfer failed: status=%d",
                                      static_cast<int>(status));
        return false;
      }
      wireTransferLength = static_cast<uint16_t>(wireTransferLength + paddedRemainingLength);
      std::memcpy(rxPayload + payloadInFirstTransfer, rxRemainder, remainingLength);
    }

    uint32_t rdyDeassertWaitMs = 0U;
    if (!waitForRdy(GPIO_PIN_RESET, kTransferTimeoutMs, rdyDeassertWaitMs)) {
      HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
      LogService::instance().logf(LogService::Level::Error,
                                    "ST67 SPI RDY deassert timeout after %lums",
                                    static_cast<unsigned long>(rdyDeassertWaitMs));
      return false;
    }

    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
    LogService::instance().logf(LogService::Level::Info,
                                  "ST67 SPI transfer stats: assert=%lums deassert=%lums txData=%uB rxData=%uB wire=%uB",
                                  static_cast<unsigned long>(rdyAssertWaitMs),
                                  static_cast<unsigned long>(rdyDeassertWaitMs),
                                  txLength,
                                  peerLength,
                                  wireTransferLength);
    rxLength = peerLength;
    return true;
  }

  // Powers up the module on first use; a no-op once modulePowered_ is set.
  bool ensureModulePowered() {
    if (modulePowered_) {
      return true;
    }

    uint8_t response[kMaxProbePayload + 1U] = {0};
    uint16_t responseLen = 0;

    HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_RESET);
    osDelay(20);

    uint32_t chipDisabledRdyWaitMs = 0U;
    if (!waitForRdy(GPIO_PIN_RESET, 100U, chipDisabledRdyWaitMs)) {
      LogService::instance().logf(LogService::Level::Error,
                                    "ST67 RDY remains high with CHIP_EN=0 after %lums; check RDY wiring or module power",
                                    static_cast<unsigned long>(chipDisabledRdyWaitMs));
      return false;
    }

    HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_SET);

    if (!transferFrame(nullptr, 0U, response, kMaxProbePayload, responseLen, 5000U)) {
      return false;
    }
    // Module may assert RDY before its "ready" text is actually queued; retry a few times.
    for (uint32_t attempt = 0U; responseLen == 0U && attempt < kReadyReadRetries; ++attempt) {
      osDelay(50);
      if (!transferFrame(nullptr, 0U, response, kMaxProbePayload, responseLen, kTransferTimeoutMs)) {
        return false;
      }
    }
    response[responseLen] = '\0';
    LogService::instance().logf(LogService::Level::Info,
                                  "ST67 startup: '%s'", response);
    modulePowered_ = true;
    return true;
  }

  void probeAtCommand() {
    static const uint8_t kAtCommand[] = "AT\r\n";
    uint8_t response[kMaxProbePayload + 1U] = {0};
    uint16_t responseLen = 0;

    if (!ensureModulePowered()) {
      return;
    }

    uint32_t atTransmitCount = 0U;
    while (atTransmitCount < kAtCommandRetries) {
      if (!transferFrame(kAtCommand,
                         sizeof(kAtCommand) - 1U,
                         response,
                         kMaxProbePayload,
                         responseLen,
                         kTransferTimeoutMs)) {
        return;
      }
      ++atTransmitCount;
      response[responseLen] = '\0';
      LogService::instance().logf(LogService::Level::Info,
                                    "ST67 AT tx attempt %lu/%lu: responseLen=%u, rx='%s'",
                                    static_cast<unsigned long>(atTransmitCount),
                                    static_cast<unsigned long>(kAtCommandRetries),
                                    responseLen,
                                    response);
      if (responseLen != 0U) {
        break;
      }
    }

    if (std::strstr(reinterpret_cast<const char*>(response), "OK") != nullptr) {
      LogService::instance().logf(LogService::Level::Info,
                                    "ST67 SPI AT probe OK after %lu transmit(s): '%s'",
                                    static_cast<unsigned long>(atTransmitCount), response);
    } else {
      LogService::instance().logf(LogService::Level::Warn,
                                    "ST67 SPI AT probe no OK after %lu transmit(s), rx='%s'",
                                    static_cast<unsigned long>(atTransmitCount), response);
    }
  }

  void probeCwlap() {
    static const uint8_t kCwMode[] = "AT+CWMODE=1\r\n";
    static const uint8_t kCwlap[] = "AT+CWLAP\r\n";
    uint8_t response[kMaxProbePayload + 1U] = {0};
    uint16_t responseLen = 0;

    if (!ensureModulePowered()) {
      return;
    }

    if (!wifiStationModeSet_) {
      if (!transferFrame(kCwMode,
                         sizeof(kCwMode) - 1U,
                         response,
                         kMaxProbePayload,
                         responseLen,
                         kTransferTimeoutMs)) {
        return;
      }
      response[responseLen] = '\0';
      LogService::instance().logf(LogService::Level::Info, "ST67 CWMODE=1: '%s'", response);
      if (std::strstr(reinterpret_cast<const char*>(response), "OK") == nullptr) {
        return;
      }
      wifiStationModeSet_ = true;
    }

    for (uint32_t scanAttempt = 1U; scanAttempt <= kCwlapScanRetries; ++scanAttempt) {
      if (!transferFrame(kCwlap,
                         sizeof(kCwlap) - 1U,
                         response,
                         kMaxProbePayload,
                         responseLen,
                         kCwlapReadyTimeoutMs)) {
        return;
      }

      size_t accumLen = 0U;
      if (responseLen != 0U) {
        accumLen = std::min<size_t>(responseLen, sizeof(cwlapBuffer_) - 1U);
        std::memcpy(cwlapBuffer_, response, accumLen);
      }
      cwlapBuffer_[accumLen] = '\0';

      uint32_t readCount = 1U;
      bool terminated = hasAtTerminator(cwlapBuffer_);
      while (!terminated && readCount < kCwlapReadAttempts &&
             accumLen < sizeof(cwlapBuffer_) - 1U) {
        uint16_t chunkLen = 0U;
        if (!transferFrame(nullptr,
                           0U,
                           response,
                           kMaxProbePayload,
                           chunkLen,
                           kCwlapReadyTimeoutMs)) {
          break;
        }
        ++readCount;
        if (chunkLen == 0U) {
          continue;
        }
        const size_t copyLen =
            std::min<size_t>(chunkLen, sizeof(cwlapBuffer_) - 1U - accumLen);
        std::memcpy(cwlapBuffer_ + accumLen, response, copyLen);
        accumLen += copyLen;
        cwlapBuffer_[accumLen] = '\0';
        terminated = hasAtTerminator(cwlapBuffer_);
      }

      const uint32_t apCount = countOccurrences(cwlapBuffer_, "+CWLAP:");
      LogService::instance().logf(LogService::Level::Info,
                                    "ST67 CWLAP attempt %lu/%lu: %lu read(s), terminated=%u, %lu AP(s)",
                                    static_cast<unsigned long>(scanAttempt),
                                    static_cast<unsigned long>(kCwlapScanRetries),
                                    static_cast<unsigned long>(readCount),
                                    terminated ? 1U : 0U,
                                    static_cast<unsigned long>(apCount));

      const char* line = reinterpret_cast<const char*>(cwlapBuffer_);
      uint32_t printedApCount = 0U;
      while (*line != '\0') {
        const char* lineEnd = std::strstr(line, "\n");
        const size_t lineLength = (lineEnd != nullptr)
                                      ? static_cast<size_t>(lineEnd - line)
                                      : std::strlen(line);
        if (std::strncmp(line, "+CWLAP:", 7U) == 0) {
          ++printedApCount;
          LogService::instance().logf(LogService::Level::Info,
                                        "ST67 AP %lu: %.*s",
                                        static_cast<unsigned long>(printedApCount),
                                        static_cast<int>(lineLength),
                                        line);
        }
        if (lineEnd == nullptr) {
          break;
        }
        line = lineEnd + 1;
      }

      if (!terminated) {
        LogService::instance().logf(LogService::Level::Warn,
                                      "ST67 CWLAP result truncated; read=%lu buffer=%lu",
                                      static_cast<unsigned long>(readCount),
                                      static_cast<unsigned long>(sizeof(cwlapBuffer_) - 1U));
        return;
      }
      if (apCount != 0U) {
        return;
      }
      if (scanAttempt < kCwlapScanRetries) {
        LogService::instance().logf(LogService::Level::Warn,
                                      "ST67 CWLAP returned no APs; retrying scan");
        osDelay(100U);
      }
    }
  }

  bool modulePowered_ = false;
  bool wifiStationModeSet_ = false;
  uint8_t cwlapBuffer_[kCwlapBufferSize] = {0};
};

}  // namespace

namespace HostController {

void StartSt67ProbeTask() {
  St67ProbeTask::instance().start();
}

void TriggerSt67Probe() {
  St67ProbeTask::instance().trigger();
}

void TriggerSt67CwlapProbe() {
  St67ProbeTask::instance().triggerCwlap();
}

}  // namespace HostController
