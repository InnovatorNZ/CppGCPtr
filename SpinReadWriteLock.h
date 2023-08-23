#pragma once

#include <atomic>
#include <thread>
#include "IReadWriteLock.h"

class SpinReadWriteLock : public IReadWriteLock {
private:
    std::atomic<int> lock_state;        // =0 unlocked, >0 reader locked, -1 writer locked
public:
    SpinReadWriteLock() : lock_state(0) {}

    void lockRead() override {
        while (true) {
            int c_read_cnt = lock_state.load(std::memory_order_acquire);
            if (c_read_cnt < 0)         // Spin until there are no active writers
                continue;
            if (lock_state.compare_exchange_weak(c_read_cnt, c_read_cnt + 1, std::memory_order_release))
                break;
            // else std::clog << "CAS read lock failed" << std::endl;
        }
    }

    void unlockRead() override {
        while (true) {
            int c_read_cnt = lock_state.load(std::memory_order_acquire);
            if (c_read_cnt <= 0) return;
            if (lock_state.compare_exchange_weak(c_read_cnt, c_read_cnt - 1, std::memory_order_release))
                break;
        }
    }

    void lockWrite(bool yield) override {
        while (true) {
            int c_lock_state = lock_state.load(std::memory_order_acquire);
            if (c_lock_state != 0) {        // Spin until there are no active readers and writers
                if (yield) std::this_thread::yield();
                continue;
            }
            if (lock_state.compare_exchange_weak(c_lock_state, -1, std::memory_order_release))
                break;
            // else std::clog << "CAS write lock failed" << std::endl;
        }
    }

    void lockWrite() override {
        lockWrite(false);
    }

    void unlockWrite() override {
        while (true) {
            int c_lock_state = lock_state.load(std::memory_order_acquire);
            if (c_lock_state >= 0) return;
            if (lock_state.compare_exchange_weak(c_lock_state, 0, std::memory_order_release))
                break;
        }
    }
};