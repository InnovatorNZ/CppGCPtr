#pragma once

#include <shared_mutex>
#include "IReadWriteLock.h"

class MutexReadWriteLock : public IReadWriteLock {
private:
    std::shared_mutex sharedMutex;
public:
    MutexReadWriteLock() = default;

    void lockRead() override {
        sharedMutex.lock_shared();
    }

    void unlockRead() override {
        sharedMutex.unlock_shared();
    }

    void lockWrite() override {
        sharedMutex.lock();
    }

    void lockWrite(bool) override {
        sharedMutex.lock();
    }

    void unlockWrite() override {
        sharedMutex.unlock();
    }
};