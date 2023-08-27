#include "GCRegion.h"

const size_t GCRegion::TINY_OBJECT_THRESHOLD = 24;
const size_t GCRegion::TINY_REGION_SIZE = 256 * 1024;
const size_t GCRegion::SMALL_OBJECT_THRESHOLD = 16 * 1024;
const size_t GCRegion::SMALL_REGION_SIZE = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_OBJECT_THRESHOLD = 1 * 1024 * 1024;
const size_t GCRegion::MEDIUM_REGION_SIZE = 32 * 1024 * 1024;

GCRegion::GCRegion(RegionEnum regionType, void* startAddress, size_t total_size) :
        regionType(regionType), startAddress(startAddress), largeRegionMarkState(MarkStateBit::NOT_ALLOCATED),
        total_size(total_size), allocated_offset(0), live_size(0), evacuated(false) {
    if (regionType != RegionEnum::LARGE) {
        if constexpr (!use_regional_hashmap) {
            switch (regionType) {
                case RegionEnum::SMALL:
                case RegionEnum::MEDIUM:
                    bitmap = std::make_unique<GCBitMap>(startAddress, total_size);
                    break;
                case RegionEnum::TINY:
                    bitmap = std::make_unique<GCBitMap>(startAddress, total_size, false);
                    break;
            }
        } else {
            regionalHashMap = std::make_unique<GCRegionalHashMap>();
        }
        if constexpr (enable_destructor) {
            destructor_map = std::make_unique<std::unordered_map<void*, std::function<void(void*)>>>();
            destructor_map->reserve(128);
        }
        if constexpr (enable_move_constructor) {
            move_constructor_map = std::make_unique<std::unordered_map<void*, std::function<void(void*, void*)>>>();
            move_constructor_map->reserve(64);
        }
    }
}

void* GCRegion::allocate(size_t size) {
    if (startAddress == nullptr || evacuated.load()) return nullptr;
    void* object_addr = nullptr;
    if (regionType == RegionEnum::TINY)
        size = TINY_OBJECT_THRESHOLD;
    else if (regionType != RegionEnum::LARGE && !use_regional_hashmap)
        size = bitmap->alignUpSize(size);
    while (true) {
        size_t p_offset = allocated_offset;
        if (p_offset + size > total_size) {
            return nullptr;
        }
        if (allocated_offset.compare_exchange_weak(p_offset, p_offset + size)) {
            object_addr = reinterpret_cast<void*>(reinterpret_cast<char*>(startAddress) + p_offset);
            break;
        }
    }
    if (GCPhase::duringGC()) {
        if constexpr (use_regional_hashmap) {
            regionalHashMap->mark(object_addr, size, GCPhase::getCurrentMarkState(), true);
        } else {
            bitmap->mark(object_addr, size, GCPhase::getCurrentMarkStateBit(), true);
        }
        live_size += size;
    } else {
        if constexpr (use_regional_hashmap) {
            if constexpr (enable_destructor)
                regionalHashMap->mark(object_addr, size, MarkState::REMAPPED, true);    // 若启用析构函数需要标记，否则不需要
        } else {
            bitmap->mark(object_addr, size, MarkStateBit::REMAPPED, true);
        }
    }
    return object_addr;
}

void GCRegion::free(void* addr, size_t size) {
    if (inside_region(addr, size)) {
        // free()要不要调用mark(addr, size, MarkStateBit::NOT_ALLOCATED)？好像还是要的
        // 现已改为mark的时候统计live_size，而不是frag_size
        // frag_size += size;
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
        if (!recently_assigned) {
            if constexpr (use_regional_hashmap) {
                regionalHashMap->mark(addr, size, MarkState::DE_ALLOCATED, false, false);
            } else {
                bitmap->mark(addr, size, MarkStateBit::NOT_ALLOCATED);
            }
        }
    } else
        std::clog << "Free address out of range." << std::endl;
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

void GCRegion::mark(void* object_addr, size_t object_size) {
    if (regionType == RegionEnum::LARGE) {
        this->largeRegionMarkState = GCPhase::getCurrentMarkStateBit();
    } else {
        if constexpr (use_regional_hashmap) {
            if (regionalHashMap->mark(object_addr, object_size, GCPhase::getCurrentMarkState()))
                live_size += object_size;
        } else {
            if (bitmap->mark(object_addr, object_size, GCPhase::getCurrentMarkStateBit()))
                live_size += object_size;
        }
    }
}

bool GCRegion::marked(void* object_addr) {
    if (regionType == RegionEnum::LARGE) {
        return largeRegionMarkState == GCPhase::getCurrentMarkStateBit();
    } else {
        if constexpr (use_regional_hashmap) {
            return regionalHashMap->getMarkState(object_addr) == GCPhase::getCurrentMarkState();
        } else {
            return bitmap->getMarkState(object_addr) == GCPhase::getCurrentMarkStateBit();
        }
    }
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
    if constexpr (use_regional_hashmap) {
        auto regionalMapIterator = regionalHashMap->getIterator();
        while (regionalMapIterator.MoveNext()) {
            GCStatus gcStatus = regionalMapIterator.current();
            const MarkState& markState = gcStatus.markState;
            if (GCPhase::needSweep(markState)) {
                // 非存活对象统一标记为DE_ALLOCATED（或者从hashmap中删除也可以），不然会导致markState经过两轮回收后重复
                regionalMapIterator.setCurrentMarkState(MarkState::DE_ALLOCATED);
                if constexpr (enable_destructor) {
                    void* addr = regionalMapIterator.getCurrentAddress();
                    callDestructor(addr);
                }
            }
        }
    } else {
        size_t _allocated_offset = allocated_offset.load();
        std::this_thread::yield();
        auto bitMapIterator = bitmap->getIterator();
        try {
            while (bitMapIterator.MoveNext() && bitMapIterator.getCurrentOffset() < _allocated_offset) {
                GCBitMap::BitStatus bitStatus = bitMapIterator.current();
                MarkStateBit& markState = bitStatus.markState;
                void* addr = reinterpret_cast<char*>(startAddress) + bitMapIterator.getCurrentOffset();
                if (GCPhase::needSweep(markState)) {    // 非存活对象，调用其析构函数，并标记为未分配，防止因M0/M1重复使用致后续误判存活
                    // 非存活对象统一标记为REMAPPED，因为仍然需要size信息遍历bitmap，并避免markState重复
                    // 但是这似乎会导致本来就是REMAPPED的对象不会被调用析构函数，现改为标记为NOT_ALLOCATED
                    bitmap->mark(addr, bitStatus.objectSize, MarkStateBit::NOT_ALLOCATED);
                    if constexpr (enable_destructor) {
                        callDestructor(addr);
                    }
                } else if (bitStatus.objectSize == 0 && regionType != RegionEnum::TINY) {
                    throw std::runtime_error("Object size found 0 in bitmap");
                }
            }
        } catch (std::runtime_error& e) {
            // 线程安全可能造成的bitmap迭代过程中抛出的异常都catch住
            std::clog << "Bitmap throw an exception: " << e.what() << std::endl;
        }
    }
}

void GCRegion::triggerRelocation(IMemoryAllocator* memoryAllocator) {
    if (regionType == RegionEnum::LARGE) {
        std::clog << "Large region doesn't need to trigger this function." << std::endl;
        return;
    }
    evacuated = true;
    if (this->canFree() && !enable_destructor) {      // 已经没有存活对象了
        return;
    }
    // std::clog << "Relocating region " << this << std::endl;
    if constexpr (use_regional_hashmap) {
        auto regionalMapIterator = regionalHashMap->getIterator();
        while (regionalMapIterator.MoveNext()) {
            GCStatus gcStatus = regionalMapIterator.current();
            const MarkState& markState = gcStatus.markState;
            void* object_addr = regionalMapIterator.getCurrentAddress();
            if (GCPhase::isLiveObject(markState)) {
                size_t object_size = regionType == RegionEnum::TINY ? TINY_OBJECT_THRESHOLD : gcStatus.objectSize;
                this->relocateObject(object_addr, object_size, memoryAllocator);
            } else if (GCPhase::needSweep(markState)) {
                if constexpr (enable_destructor) {
                    callDestructor(object_addr);
                }
            }
        }
    } else {
        auto bitMapIterator = bitmap->getIterator();
        while (bitMapIterator.MoveNext() && bitMapIterator.getCurrentOffset() < allocated_offset) {
            GCBitMap::BitStatus bitStatus = bitMapIterator.current();
            MarkStateBit& markState = bitStatus.markState;
            void* object_addr = reinterpret_cast<char*>(startAddress) + bitMapIterator.getCurrentOffset();
            if (GCPhase::isLiveObject(markState)) {     // 存活对象，转移
                unsigned int object_size = regionType == RegionEnum::TINY ? TINY_OBJECT_THRESHOLD : bitStatus.objectSize;
                this->relocateObject(object_addr, object_size, memoryAllocator);
            } else if (GCPhase::needSweep(markState)) { // 非存活对象，调用其析构函数
                // 由于region触发重定位后是不会再被使用的，因此无需再次标记
                // bitmap->mark(object_addr, bitStatus.objectSize, MarkStateBit::REMAPPED);
                if constexpr (enable_destructor) {
                    callDestructor(object_addr);
                }
            }
        }
    }
}

void GCRegion::relocateObject(void* object_addr, size_t object_size, IMemoryAllocator* memoryAllocator) {
    if (isFreed()) return;
    if (!inside_region(object_addr, object_size)) {
        std::clog << "The relocating object does not in current region." << std::endl;
        return;
    }
    std::unique_lock<std::mutex> relocate_lock(this->relocation_mutex, std::defer_lock);
    if constexpr (enable_move_constructor) {
        // 使用移动构造函数会有潜在线程安全问题，即，重分配竞争失败有可能会导致原有数据丢失
        // 因此启用移动构造函数的情况下只要触发转移对象就上锁，防止上述情况发生
        relocate_lock.lock();
    }
    {
        std::shared_lock<std::shared_mutex> lock(this->forwarding_table_mutex);
        if (forwarding_table.contains(object_addr))      // 已经被应用线程转移了
            return;
    }
    auto new_addr = memoryAllocator->allocate(object_size);
    void* new_object_addr = new_addr.first;
    std::shared_ptr<GCRegion>& new_region = new_addr.second;
    if (!this->isFreed()) {
        if constexpr (enable_move_constructor) {
            // 调用移动构造函数后立即调用析构函数析构原对象
            callMoveConstructor(object_addr, new_object_addr);
            callDestructor(object_addr);
        } else {
            ::memcpy(new_object_addr, object_addr, object_size);
        }
        // 如果在转移过程中，有应用线程访问了旧地址上的原对象并产生了写入怎么办？参考shenandoah解决方案
        std::unique_lock<std::shared_mutex> fwdtb_lock(this->forwarding_table_mutex);
        if (!forwarding_table.contains(object_addr)) {
            // std::clog << "Forwarded " << object_addr << " to " << new_object_addr << std::endl;
            forwarding_table.emplace(object_addr, new_addr);
            fwdtb_lock.unlock();
            // 将析构函数和移动构造函数注册到新region中去
            if constexpr (enable_destructor) {
                std::shared_lock<std::shared_mutex> lock2(destructor_map_mtx);
                auto it = destructor_map->find(object_addr);
                if (it != destructor_map->end()) {
                    new_region->registerDestructor(new_object_addr, it->second);
                }
            }
            if constexpr (enable_move_constructor) {
                std::shared_lock<std::shared_mutex> lock2(move_constructor_map_mtx);
                auto it = move_constructor_map->find(object_addr);
                if (it != move_constructor_map->end()) {
                    new_region->registerMoveConstructor(new_object_addr, it->second);
                }
            }
            return;
        }
    }
    // 在复制对象的过程中，已经被应用线程抢先完成了转移，撤回新分配的内存
    // std::clog << "Undoing forwarding for " << object_addr << std::endl;
    new_region->free(new_object_addr, object_size);
}

bool GCRegion::canFree() const {
    if (regionType == RegionEnum::LARGE) {
        if (GCPhase::needSweep(largeRegionMarkState)) return true;
        else return false;
    } else {
        return live_size == 0;
    }
}

bool GCRegion::needEvacuate() const {
    if (getFragmentRatio() >= 0.25 && getFreeRatio() < 0.25) return true;
    else return false;
}

void GCRegion::free() {
    // 释放整个region，只保留转发表
    evacuated = true;
    //bitmap = nullptr;     // TODO: debug结束取消注释该行
    regionalHashMap = nullptr;
    destructor_map = nullptr;
    move_constructor_map = nullptr;
    total_size = 0;
    allocated_offset = 0;
    ::free(startAddress);
    startAddress = nullptr;
}

void GCRegion::reclaim() {
    largeRegionMarkState = MarkStateBit::REMAPPED;
    if constexpr (use_regional_hashmap)
        regionalHashMap->clear();
    if constexpr (enable_destructor)
        destructor_map->clear();
    if constexpr (enable_move_constructor)
        move_constructor_map->clear();
    allocated_offset = 0;
    live_size = 0;
    evacuated = false;
}

GCRegion::GCRegion(GCRegion&& other) noexcept:
        regionType(other.regionType), startAddress(other.startAddress), total_size(other.total_size),
        bitmap(std::move(other.bitmap)), regionalHashMap(std::move(other.regionalHashMap)),
        destructor_map(std::move(other.destructor_map)), move_constructor_map(std::move(other.move_constructor_map)),
        largeRegionMarkState(other.largeRegionMarkState) {
    this->allocated_offset.store(other.allocated_offset.load());
    this->live_size.store(other.live_size.load());
    this->evacuated.store(other.evacuated.load());
    other.startAddress = nullptr;
    other.total_size = 0;
    other.allocated_offset = 0;
}

std::pair<void*, std::shared_ptr<GCRegion>> GCRegion::queryForwardingTable(void* ptr) {
    std::shared_lock<std::shared_mutex> lock(this->forwarding_table_mutex);
    auto it = forwarding_table.find(ptr);
    if (it == forwarding_table.end()) return std::make_pair(nullptr, nullptr);
    else return it->second;
}

void GCRegion::registerDestructor(void* object_addr, const std::function<void(void*)>& func) {
    if (destructor_map == nullptr) return;
    std::unique_lock<std::shared_mutex> lock(destructor_map_mtx);
    destructor_map->emplace(object_addr, func);
}

void GCRegion::registerMoveConstructor(void* object_addr, const std::function<void(void*, void*)>& func) {
    if (move_constructor_map == nullptr) return;
    std::unique_lock<std::shared_mutex> lock(move_constructor_map_mtx);
    move_constructor_map->emplace(object_addr, func);
}

void GCRegion::callDestructor(void* object_addr) {
    std::shared_lock<std::shared_mutex> lock(destructor_map_mtx);
    auto it = destructor_map->find(object_addr);
    if (it != destructor_map->end()) {
        auto& destructor = it->second;
        destructor(object_addr);
    } else
        std::clog << "Destructor not found!" << std::endl;
}

void GCRegion::callMoveConstructor(void* source_addr, void* target_addr) {
    std::shared_lock<std::shared_mutex> lock(move_constructor_map_mtx);
    auto it = move_constructor_map->find(source_addr);
    if (it != move_constructor_map->end()) {
        auto& move_constructor = it->second;
        move_constructor(source_addr, target_addr);
    }
}

bool GCRegion::inside_region(void* addr, size_t size) const {
    return (char*) addr >= (char*) startAddress
           && (char*) addr + size <= (char*) startAddress + allocated_offset;
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
            throw std::invalid_argument("Invalid region enum!");
    }
}