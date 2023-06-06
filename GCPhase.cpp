#include "GCPhase.h"

eGCPhase GCPhase::gcPhase = eGCPhase::NONE;
MarkState GCPhase::currentMarkState = MarkState::REMAPPED;
#if USE_SPINLOCK
IReadWriteLock* GCPhase::stwLock = new SpinReadWriteLock();
#else
IReadWriteLock* GCPhase::stwLock = new MutexReadWriteLock();
#endif

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

MarkStateBit GCPhase::getCurrentMarkStateBit() {
    switch (currentMarkState) {
        case MarkState::M0:
            return MarkStateBit::M0;
        case MarkState::M1:
            return MarkStateBit::M1;
        case MarkState::REMAPPED:
            return MarkStateBit::REMAPPED;
    }
}

bool GCPhase::needSweep(MarkState markState) {
    if (gcPhase != eGCPhase::SWEEP) {
        std::cerr << "Sweeping in non-sweeping phase" << std::endl;
        return false;
    }
    return currentMarkState != markState;
}

bool GCPhase::needSweep(MarkStateBit markState) {
    if (gcPhase != eGCPhase::SWEEP) {
        std::cerr << "Sweeping in non-sweeping phase" << std::endl;
        return false;
    }
    if (markState == MarkStateBit::NOT_ALLOCATED) return false;
    return markState != getCurrentMarkStateBit();
}

bool GCPhase::needSelfHeal(MarkState markState) {
    if (markState == MarkState::REMAPPED) return false;
    if (gcPhase == eGCPhase::CONCURRENT_MARK || gcPhase == eGCPhase::REMARK) {
        // 若在标记阶段，需要完成指针自愈的是上一轮存活的对象
        return markState != getCurrentMarkState();
    } else {
        // 若在转移阶段或非垃圾回收阶段，需要完成指针自愈的是本轮存活的对象
        return markState == getCurrentMarkState();
    }
}

bool GCPhase::isLiveObject(MarkStateBit markState) {
    return markState == getCurrentMarkStateBit();
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
