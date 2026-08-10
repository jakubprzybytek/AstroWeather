#include "TaskBase.hpp"
#include "FreeRTOS.h"

template <uint32_t StackSizeBytes>
class Task : public TaskBase
{
public:
    Task(const char* name, osPriority_t priority = osPriorityNormal)
        : TaskBase(name, priority, stack_, StackSizeBytes, &controlBlock_, sizeof(controlBlock_))
    {}

private:
    uint64_t stack_[StackSizeBytes / sizeof(uint64_t)];
    StaticTask_t controlBlock_;
};
