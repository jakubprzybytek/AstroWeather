#include <Display/DisplaySyncTask.hpp>

#include <Debug/LogService.hpp>
#include <Display/DisplayI2cProtocol.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <Display/Timeline.hpp>

#include "cmsis_os2.h"

#include <cstdio>

DisplaySyncTask& DisplaySyncTask::instance()
{
    static DisplaySyncTask task;
    return task;
}

// Above the normal tasks, so a due sync is not held up behind a busy one;
// it runs for a few milliseconds a minute.
DisplaySyncTask::DisplaySyncTask() : Task<2048>("DisplaySync", osPriorityAboveNormal)
{
}

void DisplaySyncTask::init(Device::I2cBus* bus, Display::Display* display)
{
    bus_ = bus;
    display_ = display;
}

void DisplaySyncTask::requestBurst()
{
    const osThreadId_t handle = getHandle();
    if (handle != nullptr) {
        osThreadFlagsSet(handle, kFlagBurst);
    }
}

DisplaySyncTask::Status DisplaySyncTask::status() const
{
    Status status{};
    status.sent = sent_;
    status.answered = answered_;
    status.lastSentTick = lastSentTick_;
    status.untilNextMs = untilNextMs_;
    status.bursts = bursts_;
    status.inBurst = inBurst_;
    for (uint8_t position = 0; position < Display::kChainLength; ++position) {
        status.boardStatus[position] = boardStatus_[position];
    }
    return status;
}

void DisplaySyncTask::run()
{
    const uint32_t start = osKernelGetTickCount();
    schedule_.start(start);
    bursts_ = 1U;
    for (;;) {
        uint32_t now = osKernelGetTickCount();
        if (schedule_.syncDue(now)) {
            sendSync(now);
            schedule_.onSyncSent(now);
        }
        if (schedule_.pollDue(now)) {
            pollBoards(now);
            schedule_.onPolled(now);
        }
        now = osKernelGetTickCount();
        inBurst_ = schedule_.inBurst();
        untilNextMs_ = schedule_.untilSyncMs(now);
        const uint32_t flags = osThreadFlagsWait(kFlagBurst, osFlagsWaitAny, schedule_.waitMs(now));
        if ((flags & osFlagsError) == 0U && (flags & kFlagBurst) != 0U) {
            startBurst(osKernelGetTickCount(), "requested");
        }
    }
}

void DisplaySyncTask::sendSync(uint32_t now)
{
    if (bus_ == nullptr) {
        return;
    }
    Display::SyncMessage message{};
    bool stamped = false;
    const HAL_StatusTypeDef status = bus_->transmitFilled(
        Display::kGeneralCallAddress, message.data(), static_cast<uint16_t>(message.size()),
        kTransferTimeoutMs, [&message, &stamped](uint8_t*) {
            Display::TimelineStamp stamp{};
            if (!Display::PcbDisplayBoard::stampNow(stamp)) {
                return;
            }
            const int64_t position = Display::positionMicros(stamp);
            const uint32_t frame = static_cast<uint32_t>(position / Display::kFrameMicros);
            const uint16_t micros = static_cast<uint16_t>(position % Display::kFrameMicros);
            Display::serializeSync(frame, micros, message);
            stamped = true;
        });
    // Unstamped (the refresh not yet started), the message was all zeros:
    // command 0x00, which the boards reject. Not counted.
    if (!stamped) {
        return;
    }
    sent_ = sent_ + 1U;
    lastSentTick_ = now;
    if (status == HAL_OK) {
        answered_ = answered_ + 1U;
    }
}

void DisplaySyncTask::pollBoards(uint32_t now)
{
    if (bus_ == nullptr || display_ == nullptr) {
        return;
    }
    uint16_t wanting = 0U;
    for (uint8_t position = 0; position < Display::kChainLength; ++position) {
        if (display_->isLocal(position)) {
            boardStatus_[position] = 0U;
            continue;
        }
        uint8_t reply = 0U;
        const HAL_StatusTypeDef status =
            bus_->receive(Display::chainAddress(position), &reply, 1U, kTransferTimeoutMs);
        boardStatus_[position] = (status == HAL_OK) ? reply : 0U;
        if (status == HAL_OK && reply == Display::kSyncStatusWanted && wanting == 0U) {
            wanting = Display::chainAddress(position);
        }
    }
    if (wanting != 0U && !schedule_.inBurst()) {
        char reason[24];
        std::snprintf(reason, sizeof(reason), "0x%02X wants syncs", static_cast<unsigned>(wanting));
        startBurst(now, reason);
    }
}

void DisplaySyncTask::startBurst(uint32_t now, const char* reason)
{
    if (schedule_.requestBurst(now)) {
        bursts_ = bursts_ + 1U;
        LogService::instance().logf(LogService::Level::Info, "Display sync: burst, %s", reason);
    }
}
