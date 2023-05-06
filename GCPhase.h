#ifndef CPPGCPTR_GCPHASE_H
#define CPPGCPTR_GCPHASE_H

#include <iostream>
#include <string>
#include <atomic>
#include <memory>
#include "PhaseEnum.h"
#include "SpinReadWriteLock.h"
#include "MutexReadWriteLock.h"

//#define USE_SPINLOCK

class GCPhase {
private:
    static eGCPhase gcPhase;
    static MarkState currentMarkState;
    static IReadWriteLock* stwLock;
public:
    static eGCPhase getGCPhase();

    static std::string getGCPhaseString();

    static MarkState getCurrentMarkState();

    static void SwitchToNextPhase();

    static bool needSweep(MarkState markState);

    static bool duringGC();

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