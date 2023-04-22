#include "GCPhase.h"

eGCPhase GCPhase::gcPhase = eGCPhase::NONE;
MarkState GCPhase::lastMarkState = MarkState::REMAPPED;

eGCPhase GCPhase::getGCPhase() {
    return gcPhase;
}

MarkState GCPhase::getLastMarkState() {
    return lastMarkState;
}

void GCPhase::switchToNextState() {
    switch (gcPhase) {
        case eGCPhase::NONE:
            switch (lastMarkState) {
                case MarkState::M0:
                    gcPhase = eGCPhase::MARK_M1;
                    break;
                case MarkState::M1:
                    gcPhase = eGCPhase::MARK_M0;
                    break;
                case MarkState::REMAPPED:
                    gcPhase = eGCPhase::MARK_M0;
                    break;
            }
            break;
        case eGCPhase::MARK_M0:
        case eGCPhase::MARK_M1:
            gcPhase = eGCPhase::SWEEP;
            lastMarkState = MarkStateUtil::flipState(lastMarkState);
            break;
        case eGCPhase::SWEEP:
            gcPhase = eGCPhase::NONE;
            break;
    }
}