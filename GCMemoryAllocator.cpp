#include "GCMemoryAllocator.h"

const size_t GCMemoryAllocator::INITIAL_SINGLE_SIZE = 8 * 1024 * 1024;
const bool GCMemoryAllocator::useConcurrentLinkedList = false;        // todo: 仅限编写代码期间注释本行

GCMemoryAllocator::GCMemoryAllocator() : GCMemoryAllocator(false) {
}

GCMemoryAllocator::GCMemoryAllocator(bool useInternalMemoryManager) {
    this->enableInternalMemoryManager = useInternalMemoryManager;
    //this->poolCount = std::thread::hardware_concurrency();
    this->poolCount = 1;
    if (useInternalMemoryManager) {
        size_t initialSize = INITIAL_SINGLE_SIZE * poolCount;
        void* initialMemory = malloc(initialSize);
        for (int i = 0; i < poolCount; i++) {
            void* c_address = reinterpret_cast<void*>(reinterpret_cast<char*>(initialMemory) + i * INITIAL_SINGLE_SIZE);
            this->memoryPools.emplace_back(c_address, INITIAL_SINGLE_SIZE);
        }
    }
    if (useConcurrentLinkedList) {
        this->smallRegionLists = std::make_unique<ConcurrentLinkedList<std::shared_ptr<GCRegion>>[]>(poolCount);
    } else {
        this->smallRegionQues = std::make_unique<std::deque<std::shared_ptr<GCRegion>>[]>(poolCount);
        this->smallRegionQueMtxs = std::make_unique<std::shared_mutex[]>(poolCount);
    }
}

void* GCMemoryAllocator::allocate(size_t size) {
    if (size <= GCRegion::TINY_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::TINY);
    } else if (size <= GCRegion::SMALL_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::SMALL);
    } else if (size <= GCRegion::MEDIUM_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::MEDIUM);
    } else {
        return this->allocate_from_region(size, RegionEnum::LARGE);
    }
}

void* GCMemoryAllocator::tryAllocateFromExistingRegion(size_t size, ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        void* addr = region->allocate(size);
        if (addr != nullptr) return addr;
    }
    return nullptr;
}

void*
GCMemoryAllocator::tryAllocateFromExistingRegion(size_t size, std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    std::shared_lock<std::shared_mutex> lock(regionQueMtx);
    for (int i = regionQue.size() - 1; i >= 0; i--) {
        void* addr = regionQue[i]->allocate(size);
        if (addr != nullptr) return addr;
    }
    return nullptr;
}

void GCMemoryAllocator::allocate_new_region(RegionEnum regionType) {
    size_t regionSize;
    switch (regionType) {
        case RegionEnum::SMALL:
            regionSize = GCRegion::SMALL_REGION_SIZE;
            break;
        case RegionEnum::MEDIUM:
            regionSize = GCRegion::MEDIUM_REGION_SIZE;
            break;
        case RegionEnum::TINY:
            regionSize = GCRegion::TINY_REGION_SIZE;
            break;
        default:
            regionSize = 0;
            break;
    }
    allocate_new_region(regionType, regionSize);
}

void GCMemoryAllocator::allocate_new_region(RegionEnum regionType, size_t regionSize) {
    // 为啥不能直接调用操作系统的malloc获取region的内存？为啥还要搞个全局freelist？
    void* new_region_memory = this->allocate_new_memory(regionSize);
    std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>(regionType, new_region_memory, regionSize);

    switch (regionType) {
        case RegionEnum::SMALL: {
            int pool_idx = getPoolIdx();
            if (useConcurrentLinkedList) {
                smallRegionLists[pool_idx].push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[pool_idx]);
                smallRegionQues[pool_idx].emplace_back(region_ptr);
            }
        }
            break;
        case RegionEnum::MEDIUM:
            if (useConcurrentLinkedList) {
                mediumRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                mediumRegionQue.emplace_back(region_ptr);
            }
            break;
        case RegionEnum::TINY:
            if (useConcurrentLinkedList) {
                tinyRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                tinyRegionQue.emplace_back(region_ptr);
            }
            break;
        case RegionEnum::LARGE:
            if (useConcurrentLinkedList) {
                largeRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(largeRegionQueMtx);
                largeRegionQue.emplace_back(region_ptr);
            }
            break;
    }
    {
        std::unique_lock<std::shared_mutex> lock(regionMapMtx);
        regionMap.emplace(new_region_memory, region_ptr);
    }
}

void* GCMemoryAllocator::allocate_from_region(size_t size, RegionEnum regionType) {
    if (size == 0) return nullptr;
    while (true) {
        // 从已有region中寻找空闲区域
        void* addr = nullptr;
        if (useConcurrentLinkedList) {
            switch (regionType) {
                case RegionEnum::SMALL:
                    addr = this->tryAllocateFromExistingRegion(size, this->smallRegionLists[getPoolIdx()]);
                    if (addr != nullptr) return addr;
                    for (int i = 0; i < poolCount; i++) {
                        addr = this->tryAllocateFromExistingRegion(size, this->smallRegionLists[i]);
                        if (addr != nullptr) return addr;
                    }
                    break;
                case RegionEnum::MEDIUM:
                    addr = this->tryAllocateFromExistingRegion(size, this->mediumRegionList);
                    break;
                case RegionEnum::TINY:
                    addr = this->tryAllocateFromExistingRegion(size, this->tinyRegionList);
                    break;
                case RegionEnum::LARGE:
                    // 分配Large的时候不会从已有region里找，直接分配新的一块region
                    break;
            }
        } else {
            switch (regionType) {
                case RegionEnum::SMALL: {
                    int pool_idx = getPoolIdx();
                    addr = this->tryAllocateFromExistingRegion(size, smallRegionQues[pool_idx], smallRegionQueMtxs[pool_idx]);
                    if (addr != nullptr) return addr;
                    for (int i = 0; i < poolCount; i++) {
                        addr = this->tryAllocateFromExistingRegion(size, this->smallRegionQues[i], this->smallRegionQueMtxs[i]);
                        if (addr != nullptr) return addr;
                    }
                }
                    break;
                case RegionEnum::MEDIUM:
                    addr = this->tryAllocateFromExistingRegion(size, this->mediumRegionQue, this->mediumRegionQueMtx);
                    break;
                case RegionEnum::TINY:
                    addr = this->tryAllocateFromExistingRegion(size, this->tinyRegionQue, this->tinyRegionQueMtx);
                    break;
                case RegionEnum::LARGE:
                    // 分配Large的时候不会从已有region里找，直接分配新的一块region
                    break;
            }
        }
        if (addr != nullptr) return addr;
        // 所有region都不够，分配新region
        if (regionType == RegionEnum::LARGE)
            this->allocate_new_region(regionType, size);
        else
            this->allocate_new_region(regionType);
    }
}

void* GCMemoryAllocator::allocate_new_memory(size_t size) {
    if (enableInternalMemoryManager)
        return this->allocate_from_freelist(size);
    else
        return malloc(size);
}

void* GCMemoryAllocator::allocate_from_freelist(size_t size) {
    int pool_idx = getPoolIdx();
    // 优先从threadLocal的memoryPool分配，若空间不足从别的steal过来，还不够则触发malloc并分配到memoryPool里
    void* address = memoryPools[pool_idx].allocate(size);
    if (address != nullptr) return address;
    for (int i = 0; i < poolCount; i++) {
        address = memoryPools[i].allocate(size);
        if (address != nullptr) return address;
    }
    do {
        size_t malloc_size = std::max(INITIAL_SINGLE_SIZE, size);
        void* new_memory = malloc(malloc_size);
        memoryPools[pool_idx].free(new_memory, malloc_size);
        address = memoryPools[pool_idx].allocate(size);
        std::clog << "Allocating more memory from OS" << std::endl;
    } while (address == nullptr);
    return address;
}

void GCMemoryAllocator::triggerClear() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger clear." << std::endl;
        return;
    }
    if (useConcurrentLinkedList) {
        clearFreeRegion(this->largeRegionList);
        for (int i = 0; i < poolCount; i++)
            clearFreeRegion(this->smallRegionLists[i]);
        clearFreeRegion(this->mediumRegionList);
        clearFreeRegion(this->tinyRegionList);
    } else {
        // 调用clearUnmarked可按region并行化
        {
            for (int i = 0; i < poolCount; i++) {
                std::shared_lock<std::shared_mutex> lock(this->smallRegionQueMtxs[i]);
                for (int j = 0; j < smallRegionQues[i].size(); j++) {
                    smallRegionQues[i][j]->clearUnmarked();
                }
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(this->mediumRegionQueMtx);
            for (int i = 0; i < mediumRegionQue.size(); i++) {
                mediumRegionQue[i]->clearUnmarked();
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(this->tinyRegionQueMtx);
            for (int i = 0; i < tinyRegionQue.size(); i++) {
                tinyRegionQue[i]->clearUnmarked();
            }
        }
        // clearFreeRegion可由四个线程并行化，但对于每种类型应单线程
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
        for (int i = 0; i < poolCount; i++)
            clearFreeRegion(smallRegionQues[i], smallRegionQueMtxs[i]);
        clearFreeRegion(mediumRegionQue, mediumRegionQueMtx);
        clearFreeRegion(tinyRegionQue, tinyRegionQueMtx);
    }
}

void GCMemoryAllocator::triggerRelocation() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger relocation." << std::endl;
        return;
    }
    if (useConcurrentLinkedList) {
        for (int i = 0; i < poolCount; i++)
            relocateRegion(this->smallRegionLists[i]);
        relocateRegion(this->mediumRegionList);
        relocateRegion(this->tinyRegionList);
        clearFreeRegion(this->largeRegionList);
    } else {
        for (int i = 0; i < poolCount; i++)
            relocateRegion(smallRegionQues[i], smallRegionQueMtxs[i]);
        relocateRegion(mediumRegionQue, mediumRegionQueMtx);
        relocateRegion(tinyRegionQue, tinyRegionQueMtx);
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
    }
}

void GCMemoryAllocator::relocateRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        if (region->needEvacuate()) {
            region->triggerRelocation(this);
        }
    }
}

void GCMemoryAllocator::relocateRegion(const std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    std::vector<std::shared_ptr<GCRegion>> regionQueSnapshot;
    regionQueSnapshot.reserve(regionQue.size() / 2);
    {
        std::shared_lock<std::shared_mutex> lock(regionQueMtx);
        for (auto& region : regionQue) {
            if (region->needEvacuate()) {
                regionQueSnapshot.emplace_back(region);
            }
        }
    }
    for (auto& region : regionQueSnapshot) {
        if (region != nullptr)
            region->triggerRelocation(this);
    }
    // TODO: 完成relocate的region应该free，包括从regionMap中移除canFree的region，以及下轮垃圾回收清理只含有转发表的region
}

void GCMemoryAllocator::clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    {
        std::shared_lock<std::shared_mutex> lock(regionQueMtx);
        for (auto& region : regionQue) {
            if (region->canFree()) {
                {
                    std::unique_lock<std::shared_mutex> lock2(this->regionMapMtx);
                    regionMap.erase(region->getStartAddr());
                }
                region->free();
            }
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(regionQueMtx);
        for (auto it = regionQue.begin(); it != regionQue.end();) {
            if (it->get()->canFree()) {
                it = regionQue.erase(it);
            } else {
                it++;
            }
        }
    }
}

void GCMemoryAllocator::clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        region->clearUnmarked();
        if (region->canFree()) {
            {
                std::unique_lock<std::shared_mutex> lock_(this->regionMapMtx);
                regionMap.erase(region->getStartAddr());
            }
            region->free();
            iterator->remove(region);
        }
    }
}

std::shared_ptr<GCRegion> GCMemoryAllocator::getRegion(void* object_addr) {
    std::shared_lock<std::shared_mutex> lock(this->regionMapMtx);
    auto it = regionMap.upper_bound(object_addr);
    if (it == regionMap.begin()) {
        return nullptr;
    } else {
        --it;
#if 0
        auto& region = it->second;
        if (!region->inside_region(object_addr))
            throw std::runtime_error("Not inside queried region!");
#endif
        return it->second;
    }
}

void GCMemoryAllocator::free(void* object_addr, size_t object_size) {
    std::shared_ptr<GCRegion> region = this->getRegion(object_addr);
    if (region != nullptr)
        region->free(object_addr, object_size);
}

void GCMemoryAllocator::resetLiveSize() {
    if (useConcurrentLinkedList) {
        for (int i = 0; i < poolCount; i++) {
            auto iterator = smallRegionLists[i].getIterator();
            while (iterator->MoveNext()) {
                iterator->current()->resetLiveSize();
            }
        }
        for (auto regionList : {&mediumRegionList, &tinyRegionList}) {
            auto iterator = regionList->getIterator();
            while (iterator->MoveNext()) {
                iterator->current()->resetLiveSize();
            }
        }
    } else {
        {
            for (int i = 0; i < poolCount; i++) {
                std::shared_lock<std::shared_mutex> lock(smallRegionQueMtxs[i]);
                for (auto& region : smallRegionQues[i]) {
                    region->resetLiveSize();
                }
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(mediumRegionQueMtx);
            for (auto& region : mediumRegionQue) {
                region->resetLiveSize();
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(tinyRegionQueMtx);
            for (auto& region : tinyRegionQue) {
                region->resetLiveSize();
            }
        }
    }
}

int GCMemoryAllocator::getPoolIdx() {
    std::thread::id tid = std::this_thread::get_id();
    int pool_idx = std::hash<std::thread::id>()(tid) % poolCount;
    return pool_idx;
}