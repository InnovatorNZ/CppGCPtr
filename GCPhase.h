#ifndef CPPGCPTR_GCPHASE_H
#define CPPGCPTR_GCPHASE_H

#include <iostream>
#include <string>
#include <atomic>
#include "PhaseEnum.h"

class GCPhase {
private:
    static eGCPhase gcPhase;
    static MarkState currentMarkState;
    static std::atomic<int> allocating_count;
public:
    static eGCPhase getGCPhase();

    static std::string getGCPhaseString();

    static MarkState getCurrentMarkState();

    static void SwitchToNextPhase();

    static bool needSweep(MarkState markState);

    static bool duringGC();

    static void EnterAllocating();

    static void LeaveAllocating();

    static bool notAllocating();
};


#endif //CPPGCPTR_GCPHASE_H