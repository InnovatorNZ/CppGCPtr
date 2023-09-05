#include "GCPhase.h"

std::atomic<eGCPhase> GCPhase::gcPhase = eGCPhase::NONE;
std::atomic<MarkState> GCPhase::currentMarkState = MarkState::REMAPPED;
SpinLock GCPhase::gcPhaseLock;
#if USE_SPINLOCK == 1
IReadWriteLock* GCPhase::stwLock = new SpinReadWriteLock();
#elif USE_SPINLOCK == 2
IReadWriteLock* GCPhase::stwLock = new WeakSpinReadWriteLock();
#else
IReadWriteLock* GCPhase::stwLock = new MutexReadWriteLock();
#endif

eGCPhase GCPhase::getGCPhase() {
    return gcPhase;
}

void GCPhase::SwitchToNextPhase() {
    switch (gcPhase) {
        case eGCPhase::NONE: {
            RAIISpinLock lock(gcPhaseLock);
            gcPhase = eGCPhase::CONCURRENT_MARK;
            currentMarkState = MarkStateUtil::switchState(currentMarkState);
        }
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

MarkStateBit GCPhase::getCurrentMarkStateBit() {
    switch (currentMarkState) {
        case MarkState::M0:
            return MarkStateBit::M0;
        case MarkState::M1:
            return MarkStateBit::M1;
        case MarkState::REMAPPED:
            return MarkStateBit::REMAPPED;
        default:
            throw std::exception();
    }
}

bool GCPhase::needSweep(MarkState markState) {
    if (markState == MarkState::DE_ALLOCATED) return false;
    return currentMarkState != markState;
}

bool GCPhase::needSweep(MarkStateBit markState) {
    if (markState == MarkStateBit::NOT_ALLOCATED) return false;
    return markState != getCurrentMarkStateBit();
}

bool GCPhase::needSelfHeal(MarkState markState) {
    gcPhaseLock.lock();
    MarkState currentMarkState = getCurrentMarkState();
    eGCPhase currentGCPhase = getGCPhase();
    gcPhaseLock.unlock();
    if (markState == MarkState::REMAPPED)       // 已重分配，无需指针自愈
        return false;
    else if (markState == MarkState::DE_ALLOCATED)  // 已被释放，不应调用此函数
        throw std::invalid_argument("DE_ALLOCATED needn't call needSelfHeal()");
    if (duringMarking(currentGCPhase)) {
        // 若在标记阶段，需要完成指针自愈的是上一轮存活的对象
        return markState != currentMarkState;
    } else {
        // 若在转移阶段或非垃圾回收阶段，需要完成指针自愈的是本轮存活的对象
        return markState == currentMarkState;
    }
}

bool GCPhase::isLiveObject(MarkStateBit markState) {
    return markState == getCurrentMarkStateBit();
}

bool GCPhase::isLiveObject(MarkState markState) {
    return markState == getCurrentMarkState();
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

GCPhase::RAIISTWLock::RAIISTWLock() {
    GCPhase::EnterCriticalSection();
}

GCPhase::RAIISTWLock::~RAIISTWLock() {
    GCPhase::LeaveCriticalSection();
}