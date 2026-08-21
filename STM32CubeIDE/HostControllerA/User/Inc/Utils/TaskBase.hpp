#pragma once

#include "cmsis_os2.h"

class TaskBase
{
public:
    using TaskVisitor = void (*)(const TaskBase& task, void* context);

    TaskBase(const char* name, osPriority_t priority, void* stackMem, 
              uint32_t stackSize, void* cbMem, uint32_t cbSize);
    virtual ~TaskBase() = default;

    void start();
    osThreadId_t getHandle() const { return handle_; }
    const char* getName() const { return name_; }
    uint32_t getStackSizeBytes() const { return stackSize_; }
    static void visitAll(TaskVisitor visitor, void* context);

protected:
    virtual void run() = 0;

private:
    static void taskEntryPoint(void* context);

    const char* name_;
    osPriority_t priority_;
    void* stackMem_;
    uint32_t stackSize_;
    void* cbMem_;
    uint32_t cbSize_;
    osThreadId_t handle_;

    static constexpr uint32_t kMaxRegisteredTasks = 16;
    static TaskBase* registeredTasks_[kMaxRegisteredTasks];
    static uint32_t registeredTaskCount_;
};
