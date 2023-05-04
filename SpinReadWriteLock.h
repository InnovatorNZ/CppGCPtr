#pragma once

#include <atomic>
#include <thread>

class SpinReadWriteLock {   // read lock priority
private:
    std::atomic<int> readerCount;
    std::atomic<bool> writerWriting;
    //std::atomic_flag writerReading = ATOMIC_FLAG_INIT;

public:
    SpinReadWriteLock() : readerCount(0), writerWriting(false) {}

    void lockRead() {
        while (writerWriting) {} // Spin until there are no active writers
        ++readerCount;
    }

    void unlockRead() {
        --readerCount;
        //writerReading.clear(std::memory_order_release);
    }

    template<bool yield = false>
    void lockWrite() {
        while (readerCount != 0) {
            if (yield) std::this_thread::yield();
        }
        while (writerWriting.exchange(true)) {} // Spin until there is no active writers
        while (readerCount != 0) {} // Spin until all readers have finished
    }

    void unlockWrite() {
        writerWriting.store(false);
    }
};