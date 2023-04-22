#ifndef CPPGCPTR_GCPTRBASE_H
#define CPPGCPTR_GCPTRBASE_H

#include "GCPhase.h"

class GCPtrBase {
private:
    MarkState markState;

public:
    GCPtrBase() : markState(MarkState::REMAPPED) {
    }

    virtual ~GCPtrBase() = default;

    void mark() {
        switch (GCPhase::getGCPhase()) {
            case eGCPhase::MARK_M0:
                markState = MarkState::M0;
                break;
            case eGCPhase::MARK_M1:
                markState = MarkState::M1;
                break;
            default:
                std::cerr << "Warning: marking at non-mark phase" << std::endl;
                break;
        }
    }

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
};


#endif //CPPGCPTR_GCPTRBASE_H