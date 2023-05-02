#include "GCPhase.h"

eGCPhase GCPhase::gcPhase = eGCPhase::NONE;
MarkState GCPhase::currentMarkState = MarkState::REMAPPED;
std::atomic<int> GCPhase::allocating_count = 0;

eGCPhase GCPhase::getGCPhase() {
    return gcPhase;
}

void GCPhase::SwitchToNextPhase() {
    switch (gcPhase) {
        case eGCPhase::NONE:
            gcPhase = eGCPhase::CONCURRENT_MARK;
            currentMarkState = MarkStateUtil::switchState(currentMarkState);
            break;
        case eGCPhase::CONCURRENT_MARK:
            gcPhase = eGCPhase::REMARK;
            break;
        case eGCPhase::REMARK:
            gcPhase = eGCPhase::SWEEP;
            break;
        case eGCPhase::SWEEP:
            gcPhase = eGCPhase::NONE;
            break;
    }
    std::clog << "GCPhase switch to " << getGCPhaseString() << std::endl;
}

MarkState GCPhase::getCurrentMarkState() {
    return currentMarkState;
}

bool GCPhase::needSweep(MarkState markState) {
    if (gcPhase != eGCPhase::SWEEP) {
        std::cerr << "Sweeping in non-sweeping phase" << std::endl;
        return false;
    }
    return currentMarkState != markState;
}

std::string GCPhase::getGCPhaseString() {
    switch (gcPhase) {
        case eGCPhase::NONE:
            return "Not GC";
        case eGCPhase::CONCURRENT_MARK:
            return "Concurrent Marking (" + MarkStateUtil::toString(currentMarkState) + ")";
        case eGCPhase::REMARK:
            return "Remarking (" + MarkStateUtil::toString(currentMarkState) + ")";
        case eGCPhase::SWEEP:
            return "Sweeping";
        default:
            return "Invalid";
    }
}

bool GCPhase::duringGC() {
    return gcPhase != eGCPhase::NONE;
}

void GCPhase::EnterAllocating() {
    allocating_count++;
}

void GCPhase::LeaveAllocating() {
    allocating_count--;
}

bool GCPhase::notAllocating() {
    return allocating_count == 0;
}