#include "GCRegion.h"

const size_t GCRegion::TINY_OBJECT_THRESHOLD = 4;
const size_t GCRegion::TINY_REGION_SIZE = 256 * 1024;
const size_t GCRegion::SMALL_OBJECT_THRESHOLD = 16 * 1024;
const size_t GCRegion::SMALL_REGION_SIZE = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_OBJECT_THRESHOLD = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_REGION_SIZE = 32 * 1024 * 1024;

GCRegion::GCRegion(RegionEnum regionType, void* startAddress, size_t total_size) :
        regionType(regionType), startAddress(startAddress), largeRegionMarkState(MarkStateBit::NOT_ALLOCATED),
        total_size(total_size), frag_size(0), c_offset(0), allFreeFlag(0), evacuated(false) {
    if (regionType != RegionEnum::LARGE) {
        bitmap = std::make_unique<GCBitMap>(startAddress, total_size);
    }
}

void* GCRegion::allocate(size_t size) {
    if (startAddress == nullptr) return nullptr;
    void* object_addr = nullptr;
    if (regionType == RegionEnum::TINY)
        size = TINY_OBJECT_THRESHOLD;
    else if (regionType != RegionEnum::LARGE)
        size = bitmap->alignUpSize(size);
    while (true) {
        size_t p_offset = c_offset;
        if (p_offset + size > total_size) return nullptr;
        if (c_offset.compare_exchange_weak(p_offset, p_offset + size)) {
            object_addr = reinterpret_cast<void*>(reinterpret_cast<char*>(startAddress) + p_offset);
            break;
        }
    }
    if (GCPhase::duringGC())
        bitmap->mark(object_addr, size, GCPhase::getCurrentMarkStateBit());
    else
        bitmap->mark(object_addr, size, MarkStateBit::REMAPPED);
    return object_addr;
}

void GCRegion::free(void* addr, size_t size) {
    if (reinterpret_cast<char*>(addr) < reinterpret_cast<char*>(startAddress) + c_offset) {
        frag_size += size;
        // TODO: free()要不要调用mark(addr, size, MarkStateBit::NOT_ALLOCATED)？答：好像还是要的
        bitmap->mark(addr, size, MarkStateBit::NOT_ALLOCATED);
    }
}

float GCRegion::getFragmentRatio() const {
    if (c_offset == 0) return 0;
    return (float) ((double) frag_size / (double) c_offset);
}

float GCRegion::getFreeRatio() const {
    if (total_size == 0) return 0;
    return (float) (1.0 - (double) c_offset / (double) total_size);
}

GCRegion::GCRegion(GCRegion&& other) : regionType(other.regionType), startAddress(other.startAddress),
                                       total_size(other.total_size), bitmap(std::move(other.bitmap)),
                                       largeRegionMarkState(other.largeRegionMarkState),
                                       allFreeFlag(other.allFreeFlag), evacuated(other.evacuated) {
    std::unique_lock lock(other.region_mtx);
    this->c_offset.store(other.c_offset.load());
    this->frag_size.store(other.frag_size.load());
    other.startAddress = nullptr;
    other.total_size = 0;
    other.c_offset = 0;
}

bool GCRegion::operator==(const GCRegion& other) const {
    return this->startAddress == other.startAddress && this->regionType == other.regionType
           && this->total_size == other.total_size;
}

void GCRegion::mark(void* object_addr, size_t object_size) {
    if (regionType == RegionEnum::LARGE)
        this->largeRegionMarkState = GCPhase::getCurrentMarkStateBit();
    else
        bitmap->mark(object_addr, object_size, GCPhase::getCurrentMarkStateBit());
}

bool GCRegion::marked(void* object_addr) const {
    if (regionType == RegionEnum::LARGE)
        return largeRegionMarkState == GCPhase::getCurrentMarkStateBit();
    else
        return bitmap->getMarkState(object_addr) == GCPhase::getCurrentMarkStateBit();
}

void GCRegion::clearUnmarked() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger clearUnmarked()" << std::endl;
        throw std::exception();
    }
    if (regionType == RegionEnum::LARGE) {
        std::clog << "Large region doesn't need to trigger this function." << std::endl;
        return;
    }
    allFreeFlag = 0;
    auto bitMapIterator = bitmap->getIterator();
    MarkStateBit lastMarkState = MarkStateBit::NOT_ALLOCATED;
    int last_offset = 0;
    while (bitMapIterator.hasNext()) {
        MarkStateBit markState = bitMapIterator.next();
        // 有必要搞single_size_set吗？？不能把single_size的放到单独的region里去？答：有道理（
        // 通过iterator统计出size，然后free
        if (GCPhase::needSweep(markState)) {
            if (regionType == RegionEnum::TINY) {
                void* addr = reinterpret_cast<char*>(startAddress) + bitMapIterator.getCurrentOffset();
                this->free(addr, TINY_OBJECT_THRESHOLD);
            } else {
                if (lastMarkState == MarkStateBit::NOT_ALLOCATED) {     // 迭代中首次
                    lastMarkState = markState;
                    last_offset = bitMapIterator.getCurrentOffset();
                } else if (lastMarkState == markState) {    // 迭代中第二次
                    int it_offset = bitMapIterator.getCurrentOffset();
                    int object_size = it_offset - last_offset + 1;
                    void* object_addr = reinterpret_cast<char*>(startAddress) + it_offset;
                    this->free(object_addr, object_size);
                    lastMarkState = MarkStateBit::NOT_ALLOCATED;
                    last_offset = it_offset;
                } else {
                    std::cerr << "Uncorrect mark found in bitmap" << std::endl;
                    throw std::exception();
                }
            }
        } else {  //有存活的对象
            allFreeFlag = -1;
        }
    }
    if (allFreeFlag == 0) allFreeFlag = 1;
}

bool GCRegion::canFree() const {
    if (regionType == RegionEnum::LARGE) {
        if (GCPhase::needSweep(largeRegionMarkState)) return true;
        else return false;
    } else {
        if (allFreeFlag == 1) return true;
        else return false;
    }
}

bool GCRegion::needEvacuate() const {
    // TODO: 被释放的region只保留转发表？
    if (getFragmentRatio() >= 0.25 && getFreeRatio() < 0.25) return true;
    else return false;
}

void GCRegion::free() {
    // 释放整个region，只保留转发表
    evacuated = true;
    bitmap = nullptr;
    total_size = 0;
    c_offset = 0;
    ::free(startAddress);
    startAddress = nullptr;
}

size_t GCRegion::GCRegionHash::operator()(const GCRegion& p) const {
    return std::hash<void*>()(p.startAddress) ^ std::hash<size_t>()(p.total_size);
}

short RegionEnumUtil::toShort(RegionEnum regionEnum) {
    switch (regionEnum) {
        case RegionEnum::SMALL:
            return 1;
        case RegionEnum::MEDIUM:
            return 2;
        case RegionEnum::LARGE:
            return 3;
        default:
            return 0;
    }
}

RegionEnum RegionEnumUtil::toRegionEnum(short e) {
    switch (e) {
        case 1:
            return RegionEnum::SMALL;
        case 2:
            return RegionEnum::MEDIUM;
        case 3:
            return RegionEnum::LARGE;
        default:
            std::cerr << "Invalid region enum!" << std::endl;
            throw std::exception();
    }
}