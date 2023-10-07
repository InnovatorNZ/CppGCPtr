#ifndef CPPGCPTR_PTRGUARD_H
#define CPPGCPTR_PTRGUARD_H

#include "GCWorker.h"
#include "GCRegion.h"

template<typename T>
class PtrGuard {
private:
    T* ptr;
    GCRegion* region;
    bool owns;
    const bool relocationEnabled;

    struct DeferGuard_t {
        explicit DeferGuard_t() = default;
    };

public:
    static constexpr DeferGuard_t DeferGuard{};

    PtrGuard(T* ptr, GCRegion* region, DeferGuard_t) :
            ptr(ptr), region(region), owns(false),
            relocationEnabled(GCWorker::getWorker()->relocationEnabled()) {
    }

    PtrGuard(T* ptr, GCRegion* region) : PtrGuard(ptr, region, DeferGuard) {
        if (relocationEnabled)
            lock();
    }

    ~PtrGuard() {
        unlock();
    }

    PtrGuard(const PtrGuard&) = delete;

    PtrGuard(PtrGuard&&) noexcept = delete;

    void lock() {
        if (!owns) {
            region->inc_use_count();
            owns = true;
        }
    }

    void unlock() {
        if (owns) {
            region->dec_use_count();
            owns = false;
        }
    }

    T* get() const {
        return ptr;
    }

    T* operator->() const {
        return ptr;
    }

    T& operator*() const {
        return *ptr;
    }

    T* value() const {
        return ptr;
    }
};

#endif //CPPGCPTR_PTRGUARD_H