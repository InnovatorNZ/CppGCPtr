#pragma once

#include <atomic>
#include <thread>

class SpinLock {
private:
    std::atomic<bool> locked_;

public:
    SpinLock() : locked_(false) {
    }

    void lock(bool yield = false) {
        while (true) {
            bool locked = this->locked_.load();
            if (locked) {
                if (yield) std::this_thread::yield();
                continue;
            }
            if (locked_.compare_exchange_weak(locked, true))
                break;
        }
    }

    void unlock() {
        locked_.store(false);
    }

    SpinLock(const SpinLock&) = delete;

    SpinLock(SpinLock&&) = delete;
};

class RAIISpinLock {
private:
    SpinLock& spinLock_;

public:
    RAIISpinLock(SpinLock& spinLock) : spinLock_(spinLock) {
        spinLock_.lock();
    }

    ~RAIISpinLock() {
        spinLock_.unlock();
    }
};