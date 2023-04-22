#ifndef CPPGCPTR_GCPHASE_H
#define CPPGCPTR_GCPHASE_H

#include "PhaseEnum.h"

class GCPhase {
private:
    static eGCPhase gcPhase;
    static MarkState lastMarkState;
public:
    static eGCPhase getGCPhase();

    static MarkState getLastMarkState();

    static void switchToNextState();
};


#endif //CPPGCPTR_GCPHASE_H