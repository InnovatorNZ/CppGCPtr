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

    /*
    bool marked() const {
        if (markState == MarkState::REMAPPED) return false;
        else {
            switch (GCPhase::getGCPhase()) {
                case eGCPhase::SWEEP:
                case eGCPhase::NONE:
                    if (markState == GCPhase::getLastMarkState()) return true;
                    else return false;
                case eGCPhase::MARK_M0:
                    if (markState == MarkState::M0) return true;
                    else return false;
                case eGCPhase::MARK_M1:
                    if (markState == MarkState::M1) return true;
                    else return false;
            }
        }
    }
    */
};


#endif //CPPGCPTR_GCPTRBASE_H