#include <Utils/Mutex.hpp>

Mutex::Mutex()
{
    const osMutexAttr_t attr{nullptr, osMutexPrioInherit, &controlBlock_, sizeof(controlBlock_)};
    handle_ = osMutexNew(&attr);
}

void Mutex::lock()
{
    osMutexAcquire(handle_, osWaitForever);
}

void Mutex::unlock()
{
    osMutexRelease(handle_);
}
