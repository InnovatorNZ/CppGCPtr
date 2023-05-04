#pragma once

#include <atomic>
#include <thread>

template<bool reader_priority, bool yield>
class SpinReadWriteLock {
private:
    std::atomic<int> readerCount;
    std::atomic<bool> writerWriting;
    std::atomic<int> lock_state;

public:
    SpinReadWriteLock() : readerCount(0), writerWriting(false) {}

    SpinReadWriteLock(const SpinReadWriteLock&) = delete;

    SpinReadWriteLock(SpinReadWriteLock&&) = delete;

    SpinReadWriteLock& operator=(const SpinReadWriteLock&) = delete;

    void lockRead() {
        while (writerWriting) { // Spin until there are no active writers
        }
        ++readerCount;
    }

    void unlockRead() {
        --readerCount;
    }

    void lockWrite() {
        if (reader_priority) {
            while (readerCount != 0) {
                if (yield) std::this_thread::yield();
            }
        }
        while (writerWriting.exchange(true)) {} // Spin until there is no active writers
        while (readerCount != 0) {} // Spin until all readers have finished
    }

    void unlockWrite() {
        writerWriting.store(false);
    }
};