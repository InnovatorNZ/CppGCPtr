#ifndef CPPGCPTR_GCSTATUS_H
#define CPPGCPTR_GCSTATUS_H

#include <cstddef>
#include "PhaseEnum.h"

struct GCStatus {
    MarkState markState;
    size_t objectSize;
};


#endif //CPPGCPTR_GCSTATUS_H