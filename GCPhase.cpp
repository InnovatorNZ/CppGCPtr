#include "GCPhase.h"

eGCPhase GCPhase::gcPhase = eGCPhase::NONE;
MarkState GCPhase::lastMarkState = MarkState::REMAPPED;

eGCPhase GCPhase::getGCPhase() {
    return gcPhase;
}

MarkState GCPhase::getLastMarkState() {
    return lastMarkState;
}

void GCPhase::switchToNextPhase() {
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
            lastMarkState = MarkStateUtil::switchState(lastMarkState);
            break;
        case eGCPhase::SWEEP:
            gcPhase = eGCPhase::NONE;
            break;
    }
    std::clog << "GCPhase switch to " << MarkStateUtil::toString(gcPhase) << std::endl;
}

MarkState GCPhase::getCurrentMarkState() {
    switch (GCPhase::getGCPhase()) {
        case eGCPhase::MARK_M0:
            return MarkState::M0;
        case eGCPhase::MARK_M1:
            return MarkState::M1;
        default:
            std::clog << "Warning: marking at non-mark phase" << std::endl;
            return MarkState::REMAPPED;
    }
}

bool GCPhase::inMarkingPhase() {
    return gcPhase == eGCPhase::MARK_M0 || gcPhase == eGCPhase::MARK_M1;
}

bool GCPhase::needSweep(MarkState markState) {
    if (gcPhase != eGCPhase::SWEEP) {
        std::cerr << "Sweeping in non-sweeping phase" << std::endl;
        return false;
    }
    return lastMarkState != markState;
}