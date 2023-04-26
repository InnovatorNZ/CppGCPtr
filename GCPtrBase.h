#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include "GCPhase.h"

#define GCPTR_IDENTIFIER 0x1f1e33fc

class GCPtrBase {
private:
    const int identifier = GCPTR_IDENTIFIER;

public:
    GCPtrBase() = default;

    virtual ~GCPtrBase() = default;

    virtual void* getVoidPtr() const = 0;
};


#endif //CPPGCPTR_GCPTRBASE_H