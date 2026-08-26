#include <St67HttpFetchTask.hpp>

#include <Debug/DebugService.hpp>
#include <HostController/St67HttpFetcher.hpp>
#include <HostController/St67NetworkSession.hpp>
#include <HostController/St67Runtime.hpp>
#include <Utils/Task.hpp>

#include "app_config.h"
#include "logging.h"

#include "FreeRTOS.h"
#include "task.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" void vLoggingPrintf(uint32_t logLevel,
                                const uint8_t metadataPrint,
                                const uint32_t lineNumber,
                                const char* const fileName,
                                const char* const format, ...) {
  (void)metadataPrint;
  (void)lineNumber;
  (void)fileName;
  char message[96];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  const DebugService::Level level =
      logLevel <= LOG_ERROR ? DebugService::Level::Error
                            : logLevel == LOG_WARN ? DebugService::Level::Warn
                                                   : DebugService::Level::Debug;
  DebugService::instance().log(level, message);
}

namespace HostController {
namespace {

constexpr uint32_t kFlagTrigger = 1U << 0;

enum class LifecycleMode : uint8_t {
  SingleFullShutdown = APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN,
  PersistentStress = APP_ST67_LIFECYCLE_PERSISTENT_STRESS,
  ColdRestartStress = APP_ST67_LIFECYCLE_COLD_RESTART_STRESS,
  HttpPersistentStress = APP_ST67_LIFECYCLE_HTTP_PERSISTENT_STRESS,
};

struct BatchResult {
  LifecycleMode mode;
  uint32_t requestedCycles = 0U;
  uint32_t attemptedCycles = 0U;
  uint32_t passedCycles = 0U;
  uint32_t failedCycles = 0U;
  uint32_t firstFailedCycle = 0U;
  const char* firstFailureStage = nullptr;
  W6X_Status_t firstFailureStatus = W6X_STATUS_OK;
  uint32_t startingHeap = 0U;
  uint32_t endingHeap = 0U;
  uint32_t lowestHeap = UINT32_MAX;
  UBaseType_t startingTasks = 0U;
  UBaseType_t endingTasks = 0U;
};

static_assert(APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN ||
                  APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_PERSISTENT_STRESS ||
                  APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_COLD_RESTART_STRESS ||
                  APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_HTTP_PERSISTENT_STRESS,
              "Unknown ST67 lifecycle mode");

void fail(St67Runtime& runtime, const char* stage) {
  if (runtime.firstFailureStage == nullptr) {
    runtime.firstFailureStage = stage;
    runtime.firstFailureStatus = runtime.lastStatus;
  }
  runtime.state = St67State::Fault;
}

bool keepsNetworkStack(LifecycleMode mode) {
  return mode == LifecycleMode::PersistentStress ||
         mode == LifecycleMode::HttpPersistentStress;
}

void publishClientResult(St67Runtime& runtime) {
  if (runtime.clientRequest == nullptr) {
    return;
  }
  St67FetchResult& result = runtime.clientRequest->result;
  result.httpStatus = static_cast<uint16_t>(runtime.httpStatus);
  result.length = 0U;
  result.crc32 = 0U;
  result.detail = static_cast<int32_t>(runtime.firstFailureStatus);
  if (runtime.firstFailureStage == nullptr) {
    result.status = St67FetchStatus::Success;
    result.length = runtime.clientPayloadLength;
    result.crc32 = runtime.httpCrc ^ 0xFFFFFFFFU;
    result.detail = 0;
  } else if (runtime.responseTooLarge) {
    result.status = St67FetchStatus::ResponseTooLarge;
  } else if (std::strcmp(runtime.firstFailureStage, "netif-stop") == 0 ||
             std::strcmp(runtime.firstFailureStage, "final-state") == 0) {
    result.status = St67FetchStatus::CleanupFailure;
  } else if (std::strcmp(runtime.firstFailureStage, "connect") == 0) {
    result.status = St67FetchStatus::NetworkFailure;
  } else if (std::strcmp(runtime.firstFailureStage, "w6x-init") == 0 ||
             std::strcmp(runtime.firstFailureStage, "wifi-init") == 0) {
    result.status = St67FetchStatus::DriverFailure;
  } else {
    result.status = St67FetchStatus::HttpFailure;
  }
  runtime.clientRequest->completed = true;
  runtime.clientRequest = nullptr;
}

class St67HttpFetchTask : public Task<2560> {
 public:
  static St67HttpFetchTask& instance() {
    static St67HttpFetchTask task;
    return task;
  }

  void trigger() {
    if (batchActive_) {
      triggerRejected_ = true;
    }
    if (getHandle() != nullptr) {
      osThreadFlagsSet(getHandle(), kFlagTrigger);
    }
  }

  bool requestClientFetch(St67FetchRequest* request) {
    if (request == nullptr || request->buffer == nullptr || request->capacity == 0U ||
        request->capacity > APP_ST67_HTTP_MAX_RESPONSE_BYTES || batchActive_ ||
        runtime_.clientRequest != nullptr) {
      if (request != nullptr) {
        request->result.status = batchActive_ || runtime_.clientRequest != nullptr
                                     ? St67FetchStatus::Busy
                                     : St67FetchStatus::InvalidArgument;
        request->completed = true;
      }
      return false;
    }
    request->result = {};
    request->result.status = St67FetchStatus::Busy;
    request->completed = false;
    runtime_.clientRequest = request;
    trigger();
    while (!request->completed) {
      osDelay(10U);
    }
    return request->result.status == St67FetchStatus::Success;
  }

 protected:
  void run() override {
    runtime_.taskHandle = getHandle();
    osDelay(APP_ST67_STARTUP_DELAY_MS);
    for (;;) {
      const uint32_t flags = osThreadFlagsWait(kFlagTrigger, osFlagsWaitAny,
                                               osWaitForever);
      if ((flags & osFlagsError) != 0U || (flags & kFlagTrigger) == 0U) {
        continue;
      }
      runBatch();
    }
  }

 private:
  St67HttpFetchTask()
      : Task<2560>("St67HttpFetch", osPriorityBelowNormal),
        network_(runtime_), fetcher_(runtime_) {}

  bool runStationIteration() {
    if (!network_.open()) {
      return false;
    }
    bool success = true;
    if (!fetcher_.fetch(runtime_.clientRequest)) {
      fail(runtime_, "fetch");
      success = false;
    }
    if (!network_.disconnect()) {
      success = false;
    }
    return success;
  }

  void logFinalResult(uint32_t cycleId) {
    DebugService::instance().logf(
        runtime_.firstFailureStage == nullptr ? DebugService::Level::Info
                                               : DebugService::Level::Error,
        "ST67 cycle=%lu result=%s stage=%s status=%d heap=%lu min=%lu tasks=%lu",
        static_cast<unsigned long>(cycleId),
        runtime_.firstFailureStage == nullptr ? "complete" : "fault",
        runtime_.firstFailureStage == nullptr ? "none" : runtime_.firstFailureStage,
        static_cast<int>(runtime_.firstFailureStage == nullptr
                             ? runtime_.lastStatus
                             : runtime_.firstFailureStatus),
        static_cast<unsigned long>(xPortGetFreeHeapSize()),
        static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()),
        static_cast<unsigned long>(uxTaskGetNumberOfTasks()));
  }

  void runBatch() {
    if (batchActive_) {
      return;
    }
    batchActive_ = true;
    osThreadFlagsClear(kFlagTrigger);
    const bool clientJob = runtime_.clientRequest != nullptr;
    const LifecycleMode mode = clientJob
                     ? LifecycleMode::PersistentStress
                     : static_cast<LifecycleMode>(APP_ST67_LIFECYCLE_MODE);
    const uint32_t requestedCycles =
      clientJob
        ? 1U
        : mode == LifecycleMode::PersistentStress
                  ? APP_ST67_PERSISTENT_STRESS_CYCLES
                  : mode == LifecycleMode::HttpPersistentStress
                        ? APP_ST67_HTTP_PERSISTENT_STRESS_CYCLES
                        : APP_ST67_COLD_RESTART_STRESS_CYCLES;
    BatchResult result{mode, requestedCycles};
    result.startingHeap = xPortGetFreeHeapSize();
    result.lowestHeap = xPortGetMinimumEverFreeHeapSize();
    result.startingTasks = uxTaskGetNumberOfTasks();
    bool initialized = false;
    for (uint32_t cycle = 0U; cycle < requestedCycles; ++cycle) {
      ++result.attemptedCycles;
      ++cycleId_;
      runtime_.firstFailureStage = nullptr;
      runtime_.firstFailureStatus = W6X_STATUS_OK;
      if (!keepsNetworkStack(mode) && mode == LifecycleMode::ColdRestartStress) {
        initialized = false;
      }
      if (!initialized) {
        initialized = network_.initialize(result.attemptedCycles == 1U);
        if (!initialized) {
          network_.stop();
        }
      }
      const bool ready = initialized &&
                         (!keepsNetworkStack(mode) || cycle == 0U || network_.isReady());
      if (!ready) {
        fail(runtime_, "persistent-ready");
      }
      const bool iterationPassed = ready && runStationIteration();
      const bool teardownPassed = keepsNetworkStack(mode) ? true : network_.stop();
      if (!keepsNetworkStack(mode)) {
        initialized = false;
      }
      if (iterationPassed && teardownPassed && runtime_.firstFailureStage == nullptr) {
        ++result.passedCycles;
      } else {
        ++result.failedCycles;
        if (result.firstFailedCycle == 0U) {
          result.firstFailedCycle = cycleId_;
          result.firstFailureStage = runtime_.firstFailureStage;
          result.firstFailureStatus = runtime_.firstFailureStatus;
        }
      }
      const uint32_t currentHeap = xPortGetFreeHeapSize();
      result.lowestHeap = currentHeap < result.lowestHeap ? currentHeap : result.lowestHeap;
      const uint32_t minimumEverHeap = xPortGetMinimumEverFreeHeapSize();
      result.lowestHeap = minimumEverHeap < result.lowestHeap ? minimumEverHeap : result.lowestHeap;
      logFinalResult(cycleId_);
      if (!ready || !teardownPassed) {
        break;
      }
      if (keepsNetworkStack(mode) && cycle + 1U < requestedCycles) {
        osDelay(APP_ST67_INTER_CYCLE_DELAY_MS);
      } else if (mode == LifecycleMode::ColdRestartStress && cycle + 1U < requestedCycles) {
        osDelay(APP_ST67_COLD_RESTART_DELAY_MS);
      }
    }
    if (keepsNetworkStack(mode) && !clientJob) {
      const bool teardownPassed = network_.stop();
      if (!teardownPassed && result.firstFailedCycle == 0U) {
        result.firstFailedCycle = cycleId_;
        result.firstFailureStage = runtime_.firstFailureStage;
        result.firstFailureStatus = runtime_.firstFailureStatus;
        ++result.failedCycles;
      }
    }
    result.endingHeap = xPortGetFreeHeapSize();
    result.endingTasks = uxTaskGetNumberOfTasks();
    DebugService::instance().logf(
        result.failedCycles == 0U ? DebugService::Level::Info : DebugService::Level::Error,
        "ST67 batch-final mode=%u pass=%lu fail=%lu first=%lu stage=%s status=%d heap=%lu/%lu min=%lu tasks=%lu/%lu",
        static_cast<unsigned int>(mode), static_cast<unsigned long>(result.passedCycles),
        static_cast<unsigned long>(result.failedCycles),
        static_cast<unsigned long>(result.firstFailedCycle),
        result.firstFailureStage == nullptr ? "none" : result.firstFailureStage,
        static_cast<int>(result.firstFailureStatus),
        static_cast<unsigned long>(result.startingHeap),
        static_cast<unsigned long>(result.endingHeap),
        static_cast<unsigned long>(result.lowestHeap),
        static_cast<unsigned long>(result.startingTasks),
        static_cast<unsigned long>(result.endingTasks));
    publishClientResult(runtime_);
    if (triggerRejected_) {
      DebugService::instance().log(DebugService::Level::Warn,
                                   "ST67 batch trigger rejected: active");
      triggerRejected_ = false;
    }
    osThreadFlagsClear(kFlagTrigger);
    batchActive_ = false;
  }

  St67Runtime runtime_{};
  St67NetworkSession network_;
  St67HttpFetcher fetcher_;
  uint32_t cycleId_ = 0U;
  bool batchActive_ = false;
  bool triggerRejected_ = false;
};

}  // namespace

void StartSt67HttpFetchTask() { St67HttpFetchTask::instance().start(); }

void TriggerSt67SmokeTest() { TriggerSt67ConnectivityCycle(); }

void TriggerSt67ConnectivityCycle() { St67HttpFetchTask::instance().trigger(); }

bool FetchSt67Data(St67FetchRequest* request) {
  return St67HttpFetchTask::instance().requestClientFetch(request);
}

}  // namespace HostController
