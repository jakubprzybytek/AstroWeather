#pragma once

#include "cmsis_os2.h"
#include "FreeRTOS.h"

// Static, non-recursive CMSIS-RTOS2 mutex with priority inheritance. Safe to
// construct as a global/static object; the control block is never heap-allocated.
class Mutex
{
public:
    Mutex();

    void lock();
    void unlock();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
    StaticSemaphore_t controlBlock_{};
    osMutexId_t handle_ = nullptr;
};

// RAII scope guard: locks on construction, unlocks on destruction.
class MutexGuard
{
public:
    explicit MutexGuard(Mutex& mutex) : mutex_(mutex) { mutex_.lock(); }
    ~MutexGuard() { mutex_.unlock(); }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    Mutex& mutex_;
};
