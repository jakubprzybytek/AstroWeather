#include <Utils/TaskBase.hpp>

TaskBase::TaskBase(const char* name, osPriority_t priority, void* stackMem,
                    uint32_t stackSize, void* cbMem, uint32_t cbSize)
    : name_(name), priority_(priority), stackMem_(stackMem), stackSize_(stackSize),
      cbMem_(cbMem), cbSize_(cbSize), handle_(nullptr)
{}

void TaskBase::start()
{
    osThreadAttr_t attr = {};
    attr.name      = name_;
    attr.priority  = priority_;
    attr.stack_mem = stackMem_;
    attr.stack_size= stackSize_;
    attr.cb_mem    = cbMem_;
    attr.cb_size   = cbSize_;

    handle_ = osThreadNew(&TaskBase::taskEntryPoint, this, &attr);
}

void TaskBase::taskEntryPoint(void* context)
{
    static_cast<TaskBase*>(context)->run();
}
