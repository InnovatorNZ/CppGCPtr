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
public:
    static std::shared_ptr<IReadWriteLock> stwLock;

    static eGCPhase getGCPhase();

    static std::string getGCPhaseString();

    static MarkState getCurrentMarkState();

    static void SwitchToNextPhase();

    static bool needSweep(MarkState markState);

    static bool duringGC();

    static inline void EnterCriticalSection() {
        stwLock->lockRead();
    }

    static inline void LeaveCriticalSection() {
        stwLock->unlockRead();
    }
};


#endif //CPPGCPTR_GCPHASE_H