#include <Utils/TaskBase.hpp>

TaskBase* TaskBase::registeredTasks_[TaskBase::kMaxRegisteredTasks] = {};
uint32_t TaskBase::registeredTaskCount_ = 0;

TaskBase::TaskBase(const char* name, osPriority_t priority, void* stackMem,
                    uint32_t stackSize, void* cbMem, uint32_t cbSize)
    : name_(name), priority_(priority), stackMem_(stackMem), stackSize_(stackSize),
      cbMem_(cbMem), cbSize_(cbSize), handle_(nullptr)
{
    if (registeredTaskCount_ < kMaxRegisteredTasks)
    {
        registeredTasks_[registeredTaskCount_++] = this;
    }
}

void TaskBase::visitAll(TaskVisitor visitor, void* context)
{
    if (visitor == nullptr)
    {
        return;
    }

    for (uint32_t index = 0; index < registeredTaskCount_; ++index)
    {
        visitor(*registeredTasks_[index], context);
    }
}

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
