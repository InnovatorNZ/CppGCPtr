#pragma once

#include <shared_mutex>
#include <stdexcept>
#include "IReadWriteLock.h"

class MutexReadWriteLock : public IReadWriteLock {
private:
    std::shared_mutex sharedMutex;
    static thread_local int read_locked_cnt;
public:
    MutexReadWriteLock() = default;

    void lockRead() override;

    void unlockRead() override;

    void lockWrite() override;

    void lockWrite(bool) override;

    void unlockWrite() override;
};