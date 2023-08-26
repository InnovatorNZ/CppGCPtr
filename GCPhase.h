#ifndef CPPGCPTR_GCPHASE_H
#define CPPGCPTR_GCPHASE_H

#include <iostream>
#include <string>
#include <atomic>
#include <memory>
#include "PhaseEnum.h"
#include "SpinReadWriteLock.h"
#include "MutexReadWriteLock.h"
#include "WeakSpinReadWriteLock.h"

#define USE_SPINLOCK 0

class GCPhase {
private:
    static std::atomic<eGCPhase> gcPhase;
    static std::atomic<MarkState> currentMarkState;
    static IReadWriteLock* stwLock;
public:
    static eGCPhase getGCPhase();

    static std::string getGCPhaseString();

    static MarkState getCurrentMarkState() {
        return currentMarkState;
    }

    static MarkStateBit getCurrentMarkStateBit();

    static void SwitchToNextPhase();

    static bool needSweep(MarkState markState);

    static bool needSweep(MarkStateBit markState);

    static bool needSelfHeal(MarkState markState);

    static bool isLiveObject(MarkStateBit);

    static bool isLiveObject(MarkState);

    static bool duringGC() {
        return gcPhase != eGCPhase::NONE;
    }

    static bool duringMarking() {
        return gcPhase == eGCPhase::CONCURRENT_MARK || gcPhase == eGCPhase::REMARK;
    }

    static void EnterCriticalSection() {
        stwLock->lockRead();
    }

    static void LeaveCriticalSection() {
        stwLock->unlockRead();
    }

    static IReadWriteLock* getSTWLock() {
        return stwLock;
    }
};


#endif //CPPGCPTR_GCPHASE_H