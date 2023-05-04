#pragma once

#include <atomic>
#include <thread>

class SpinReadWriteLock {
private:
    std::atomic<int> lock_state;        // =0 unlocked, >0 reader locked, -1 writer locked
public:
    SpinReadWriteLock() : lock_state(0) {}

    SpinReadWriteLock(const SpinReadWriteLock&) = delete;

    SpinReadWriteLock(SpinReadWriteLock&&) = delete;

    SpinReadWriteLock& operator=(const SpinReadWriteLock&) = delete;

    void lockRead() {
        while (true) {
            int c_read_cnt = lock_state;
            if (c_read_cnt < 0)         // Spin until there are no active writers
                continue;
            if (lock_state.compare_exchange_weak(c_read_cnt, c_read_cnt + 1))
                break;
            else
                std::clog << "CAS read lock failed" << std::endl;
        }
    }

    void unlockRead() {
        while (true) {
            int c_read_cnt = lock_state;
            if (c_read_cnt <= 0) return;
            if (lock_state.compare_exchange_weak(c_read_cnt, c_read_cnt - 1))
                break;
        }
    }

    void lockWrite(bool yield) {
        while (true) {
            int c_lock_state = lock_state;
            if (c_lock_state != 0) {        // Spin until there are no active readers and writers
                if (yield) std::this_thread::yield();
                continue;
            }
            if (lock_state.compare_exchange_weak(c_lock_state, -1))
                break;
            else
                std::clog << "CAS write lock failed" << std::endl;
        }
    }

    void unlockWrite() {
        while (true) {
            int c_lock_state = lock_state;
            if (c_lock_state >= 0) return;
            if (lock_state.compare_exchange_weak(c_lock_state, 0))
                break;
        }
    }
};