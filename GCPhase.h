#ifndef CPPGCPTR_GCPHASE_H
#define CPPGCPTR_GCPHASE_H

#include <iostream>
#include "PhaseEnum.h"

class GCPhase {
private:
    static eGCPhase gcPhase;
    static MarkState lastMarkState;
public:
    static eGCPhase getGCPhase();

    static MarkState getLastMarkState();

    static MarkState getCurrentMarkState();

    static void switchToNextPhase();

    static bool inMarkingPhase();

    static bool needSweep(MarkState markState);
};


#endif //CPPGCPTR_GCPHASE_H