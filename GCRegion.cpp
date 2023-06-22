#include "GCRegion.h"

const size_t GCRegion::TINY_OBJECT_THRESHOLD = 24;
const size_t GCRegion::TINY_REGION_SIZE = 256 * 1024;
const size_t GCRegion::SMALL_OBJECT_THRESHOLD = 16 * 1024;
const size_t GCRegion::SMALL_REGION_SIZE = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_OBJECT_THRESHOLD = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_REGION_SIZE = 32 * 1024 * 1024;

GCRegion::GCRegion(RegionEnum regionType, void* startAddress, size_t total_size) :
        regionType(regionType), startAddress(startAddress), largeRegionMarkState(MarkStateBit::NOT_ALLOCATED),
        total_size(total_size), allocated_offset(0), live_size(0), allFreeFlag(0), evacuated(false) {
    switch (regionType) {
        case RegionEnum::SMALL:
        case RegionEnum::MEDIUM:
            bitmap = std::make_unique<GCBitMap>(startAddress, total_size);
            break;
        case RegionEnum::TINY:
            bitmap = std::make_unique<GCBitMap>(startAddress, total_size, false);
            break;
        default:
            bitmap = nullptr;
            break;
    }
}

void* GCRegion::allocate(size_t size) {
    if (startAddress == nullptr || evacuated.load()) return nullptr;
    void* object_addr = nullptr;
    if (regionType == RegionEnum::TINY)
        size = TINY_OBJECT_THRESHOLD;
    else if (regionType != RegionEnum::LARGE)
        size = bitmap->alignUpSize(size);
    while (true) {
        size_t p_offset = allocated_offset;
        if (p_offset + size > total_size) return nullptr;
        if (allocated_offset.compare_exchange_weak(p_offset, p_offset + size)) {
            object_addr = reinterpret_cast<void*>(reinterpret_cast<char*>(startAddress) + p_offset);
            break;
        }
    }
    if (GCPhase::duringGC()) {
        if (bitmap->mark(object_addr, size, GCPhase::getCurrentMarkStateBit()))
            live_size += size;
    } else {
        bitmap->mark(object_addr, size, MarkStateBit::REMAPPED);
    }
    return object_addr;
}

void GCRegion::free(void* addr, size_t size) {
    if (reinterpret_cast<char*>(addr) < reinterpret_cast<char*>(startAddress) + allocated_offset) {
        // free()要不要调用mark(addr, size, MarkStateBit::NOT_ALLOCATED)？好像还是要的
        // 现已改为mark的时候统计live_size，而不是frag_size，因此free时不能再mark为NOT_ALLOCATED，而是mark为REMAPPED
        // frag_size += size;
#if _DEBUG      // todo: 由于不再标记高位（？），大概要删掉这段了
        auto markstate = bitmap->getMarkState(addr);
        auto markstate2 = bitmap->getMarkState((char*)addr + size - 1);
        if (markstate == MarkStateBit::NOT_ALLOCATED ||
            regionType != RegionEnum::TINY && markstate2 == MarkStateBit::NOT_ALLOCATED) {
            std::clog << "Why free not allocated area??" << std::endl;
            throw std::exception();
        }
#endif
        bool recently_assigned;
        while (true) {
            size_t c_allocated_offset = allocated_offset.load();
            if (reinterpret_cast<char*>(addr) + size == reinterpret_cast<char*>(startAddress) + c_allocated_offset) {
                // 如果欲销毁的空间正好是region刚刚分配的，则撤销该分配（即减去allocated_offset）
                if (allocated_offset.compare_exchange_weak(c_allocated_offset, c_allocated_offset - size)) {
                    recently_assigned = true;
                    break;
                }
            } else {
                recently_assigned = false;
                break;
            }
        }
        if (!recently_assigned)
            bitmap->mark(addr, size, MarkStateBit::REMAPPED);
        std::clog << "free(addr,size) triggered in GCRegion" << std::endl;
    } else std::clog << "Free address out of range." << std::endl;
}

float GCRegion::getFragmentRatio() const {
    if (allocated_offset == 0) return 0;
    size_t frag_size = allocated_offset - live_size;
    return (float) ((double) frag_size / (double) allocated_offset);
}

float GCRegion::getFreeRatio() const {
    if (total_size == 0) return 0;
    return (float) (1.0 - (double) allocated_offset / (double) total_size);
}

GCRegion::GCRegion(GCRegion&& other) : regionType(other.regionType), startAddress(other.startAddress),
                                       total_size(other.total_size), bitmap(std::move(other.bitmap)),
                                       largeRegionMarkState(other.largeRegionMarkState),
                                       allFreeFlag(other.allFreeFlag) {
    std::unique_lock lock(other.region_mtx);
    this->allocated_offset.store(other.allocated_offset.load());
    //this->frag_size.store(other.frag_size.load());
    this->live_size.store(other.live_size.load());
    this->evacuated.store(other.evacuated.load());
    other.startAddress = nullptr;
    other.total_size = 0;
    other.allocated_offset = 0;
}

bool GCRegion::operator==(const GCRegion& other) const {
    return this->startAddress == other.startAddress && this->regionType == other.regionType
           && this->total_size == other.total_size;
}

void GCRegion::mark(void* object_addr, size_t object_size) {
    if (regionType == RegionEnum::LARGE) {
        this->largeRegionMarkState = GCPhase::getCurrentMarkStateBit();
    } else {
        if (bitmap->mark(object_addr, object_size, GCPhase::getCurrentMarkStateBit()))
            live_size += object_size;
    }
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
    // live_objects.clear();        // live_objects好像没什么用了
    auto bitMapIterator = bitmap->getIterator();
    // int last_offset = 0; MarkStateBit lastMarkState = MarkStateBit::NOT_ALLOCATED;
    while (bitMapIterator.MoveNext()) {
        GCBitMap::BitStatus bitStatus = bitMapIterator.current();
        MarkStateBit& markState = bitStatus.markState;
        // 有必要搞single_size_set吗？？不能把single_size的放到单独的region里去？答：有道理（
        // 通过iterator遍历筛选出存活的对象
        void* addr = reinterpret_cast<char*>(startAddress) + bitMapIterator.getCurrentOffset();
        if (GCPhase::isLiveObject(markState)) {
#if 0
            if (regionType == RegionEnum::TINY) {
                live_objects.emplace_back(addr, TINY_OBJECT_THRESHOLD);
            } else {
                live_objects.emplace_back(addr, bitStatus.objectSize);
#if 0
                if (lastMarkState == MarkStateBit::NOT_ALLOCATED) {     // 迭代中首次
                    lastMarkState = markState;
                    last_offset = bitMapIterator.getCurrentOffset();
                } else if (lastMarkState == markState) {    // 迭代中第二次
                    int c_offset = bitMapIterator.getCurrentOffset();
                    int object_size = c_offset - last_offset + 1;
                    void* object_addr = reinterpret_cast<char*>(startAddress) + last_offset;
                    this->free(object_addr, object_size);
                    lastMarkState = MarkStateBit::NOT_ALLOCATED;
                    last_offset = c_offset;
                } else {
                    std::cerr << "Uncorrect mark found in bitmap! current: " <<
                        MarkStateUtil::toString(markState) << ", last: " << MarkStateUtil::toString(lastMarkState);
                    auto last_markstate_recheck = bitmap->getMarkState((char*)startAddress + last_offset);
                    auto current_markstate_recheck = bitmap->getMarkState((char*)startAddress + bitMapIterator.getCurrentOffset());
                    std::cerr << ". Rechecked current: " << MarkStateUtil::toString(current_markstate_recheck) <<
                        ", rechecked last: " << MarkStateUtil::toString(last_markstate_recheck);
                    std::cerr << ". offset delta: " << bitMapIterator.getCurrentOffset() - last_offset << std::endl;
                    throw std::exception();
                }
#endif
            }
#endif
            allFreeFlag = -1;
        } else if (GCPhase::needSweep(markState) && markState != MarkStateBit::REMAPPED) {
            // 非存活对象统一标记为REMAPPED
            // 之所以不能标记为NOT_ALLOCATED是因为仍然需要size信息遍历bitmap
            bitmap->mark(addr, bitStatus.objectSize, MarkStateBit::REMAPPED);
        } else if (markState == MarkStateBit::NOT_ALLOCATED) {
            if (bitMapIterator.getCurrentOffset() >= allocated_offset) break;
            else if (regionType == RegionEnum::TINY);
            else throw std::exception();    // 多线程情况下可能会误判
        }
    }
    if (allFreeFlag == 0) allFreeFlag = 1;
}

void GCRegion::triggerRelocation(IAllocatable* memoryAllocator) {
    if (regionType == RegionEnum::LARGE) {
        std::clog << "Large region doesn't need to trigger this function." << std::endl;
        return;
    }
    evacuated.store(true);
    if (this->canFree()) {      // 已经没有存活对象了
        this->free();
        return;
    }
    std::clog << "Relocating region " << this << std::endl;
    auto bitMapIterator = bitmap->getIterator();
    while (bitMapIterator.MoveNext()) {
        GCBitMap::BitStatus bitStatus = bitMapIterator.current();
        MarkStateBit& markState = bitStatus.markState;
        void* object_addr = reinterpret_cast<char*>(startAddress) + bitMapIterator.getCurrentOffset();
        if (GCPhase::isLiveObject(markState)) {     // 筛选出存活对象并转移
            unsigned int object_size = regionType == RegionEnum::TINY ? TINY_OBJECT_THRESHOLD : bitStatus.objectSize;
            this->relocateObject(object_addr, object_size, memoryAllocator);
        } else if (GCPhase::needSweep(markState) && markState != MarkStateBit::REMAPPED) {
            // 非存活对象统一标记为REMAPPED；之所以不能标记为NOT_ALLOCATED是因为仍然需要size信息遍历bitmap
            bitmap->mark(object_addr, bitStatus.objectSize, MarkStateBit::REMAPPED);
        } else if (markState == MarkStateBit::NOT_ALLOCATED) {
            if (bitMapIterator.getCurrentOffset() >= allocated_offset) break;
            else if (regionType == RegionEnum::TINY);
            else throw std::exception();    // 多线程情况下可能会误判
        }
    }
    this->free();       // todo: 要不要free()？
}

void GCRegion::relocateObject(void* object_addr, size_t object_size, IAllocatable* allocator) {
    if (!inside_region(object_addr, object_size)) {
        std::clog << "The relocating object does not in current region!" << std::endl;
        throw std::exception();
        return;
    }
    {
        std::shared_lock<std::shared_mutex> lock(this->forwarding_table_mutex);
        if (forwarding_table.contains(object_addr))      // 已经被应用线程转移了
            return;
    }
    void* new_addr = allocator->allocate(object_size);
    ::memcpy(new_addr, object_addr, object_size);
    {
        // 如果在转移过程中，有应用线程访问了旧地址上的原对象并产生了写入怎么办？参考shenandoah解决方案
        std::unique_lock<std::shared_mutex> lock(this->forwarding_table_mutex);
        if (!forwarding_table.contains(object_addr)) {
            forwarding_table.emplace(object_addr, new_addr);
            std::clog << "Forwarding " << object_addr << " to " << new_addr << std::endl;
        } else {
            // 在复制对象的过程中，已经被应用线程抢先完成了转移，撤回新分配的内存
            lock.unlock();
            std::clog << "Cancelling relocation due to someone beats us for " << object_addr << std::endl;
            allocator->free(new_addr, object_size);
        }
    }
}

bool GCRegion::canFree() const {
    if (regionType == RegionEnum::LARGE) {
        if (GCPhase::needSweep(largeRegionMarkState)) return true;
        else return false;
    } else {
        if (allFreeFlag == 0 && live_size == 0) return true;
        else if (allFreeFlag == 1) return true;
        else return false;
    }
}

bool GCRegion::needEvacuate() const {
    if (getFragmentRatio() >= 0.25 && getFreeRatio() < 0.25) return true;
    else return false;
}

void GCRegion::free() {
    std::clog << "Freeing region " << this << std::endl;
    // 释放整个region，只保留转发表
    evacuated = true;
    // TODO: debug完成后请取消注释以下几行并还原
#if _DEBUG
    if (regionType != RegionEnum::LARGE && debug_not_deleted != 79) {
        ::free(startAddress);
        //bitmap = nullptr;
        debug_not_deleted = 79;
    }
#else
    bitmap = nullptr;
    total_size = 0;
    allocated_offset = 0;
    ::free(startAddress);
    startAddress = nullptr;
#endif
}

void* GCRegion::queryForwardingTable(void* ptr) {
    std::shared_lock<std::shared_mutex> lock(this->forwarding_table_mutex);
    auto it = forwarding_table.find(ptr);
    if (it == forwarding_table.end()) return nullptr;
    else return it->second;
}

bool GCRegion::inside_region(void* addr, size_t size) const {
    return (char*)addr >= (char*)startAddress
        && (char*)addr + size <= (char*)startAddress + allocated_offset;
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