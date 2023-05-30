#include "GCMemoryAllocator.h"

GCMemoryAllocator::GCMemoryAllocator() : GCMemoryAllocator(false) {
}

GCMemoryAllocator::GCMemoryAllocator(bool useInternalMemoryManager) {
    this->enableInternalMemoryManager = useInternalMemoryManager;
    if (useInternalMemoryManager) {
        this->poolCount = std::thread::hardware_concurrency();
        size_t initialSize = INITIAL_SINGLE_SIZE * poolCount;
        void* initialMemory = malloc(initialSize);
        for (int i = 0; i < poolCount; i++) {
            void* c_address = reinterpret_cast<void*>(reinterpret_cast<char*>(initialMemory) + i * INITIAL_SINGLE_SIZE);
            this->memoryPools.emplace_back(c_address, INITIAL_SINGLE_SIZE);
        }
    } else {
        this->poolCount = 0;
    }
}

void* GCMemoryAllocator::allocate(size_t size) {
    if (size <= TINY_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::TINY);
    } else if (size <= SMALL_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::SMALL);
    } else if (size <= MEDIUM_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::MEDIUM);
    } else {
        return this->allocate_from_region(size, RegionEnum::LARGE);
    }
}

void* GCMemoryAllocator::allocate_from_region(size_t size, RegionEnum regionType) {
    if (size == 0) return nullptr;
    if (regionType == RegionEnum::SMALL) {
        while (true) {
            // 从已有region中寻找空闲区域
            {
                std::shared_lock<std::shared_mutex> lock(smallRegionQueMtx);
                for (int i = smallRegionQue.size() - 1; i >= 0; i--) {
                    void* addr = smallRegionQue[i]->allocate(size);
                    if (addr != nullptr) return addr;
                }
            }
            // 所有region都不够，分配新region
            // TODO: 我为啥不能直接调用操作系统的malloc获取region的内存？？为啥还要搞个全局freelist？？
            // TODO: 调查bitmap究竟怎么和region/freelist配合工作
            void* new_region_memory = this->allocate_new_memory(SMALL_REGION_SIZE);
            std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>
                    (RegionEnum::SMALL, new_region_memory, SMALL_REGION_SIZE);
            {
                std::unique_lock<std::shared_mutex> lock(smallRegionQueMtx);
                smallRegionQue.emplace_back(region_ptr);
            }
            {
                std::unique_lock<std::shared_mutex> lock(regionMapMtx);
                regionMap.emplace(new_region_memory, region_ptr);
            }
        }
    } else if (regionType == RegionEnum::MEDIUM) {
        while (true) {
            {
                std::shared_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                for (int i = mediumRegionQue.size() - 1; i >= 0; i--) {
                    void* addr = mediumRegionQue[i]->allocate(size);
                    if (addr != nullptr) return addr;
                }
            }
            void* new_region_memory = this->allocate_new_memory(MEDIUM_REGION_SIZE);
            std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>
                    (RegionEnum::MEDIUM, new_region_memory, MEDIUM_REGION_SIZE);
            {
                std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                mediumRegionQue.emplace_back(region_ptr);
            }
            {
                std::unique_lock<std::shared_mutex> lock(regionMapMtx);
                regionMap.emplace(new_region_memory, region_ptr);
            }
        }
    } else if (regionType == RegionEnum::LARGE) {
        // 分配Large的时候不会从已有region里找，直接分配新的一块region
        void* new_region_memory = this->allocate_new_memory(size);
        std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>
                (RegionEnum::LARGE, new_region_memory, size);
        {
            std::unique_lock<std::shared_mutex> lock(largeRegionQueMtx);
            largeRegionQue.emplace_back(region_ptr);
        }
        {
            std::unique_lock<std::shared_mutex> lock(regionMapMtx);
            regionMap.emplace(new_region_memory, region_ptr);
        }
    } else if (regionType == RegionEnum::TINY) {
        while (true) {
            {
                std::shared_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                for (int i = tinyRegionQue.size() - 1; i >= 0; i--) {
                    void* addr = tinyRegionQue[i]->allocate(size);
                    if (addr != nullptr) return addr;
                }
            }
            void* new_region_memory = this->allocate_new_memory(TINY_REGION_SIZE);
            std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>
                    (RegionEnum::TINY, new_region_memory, TINY_REGION_SIZE);
            {
                std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                tinyRegionQue.emplace_back(region_ptr);
            }
            {
                std::unique_lock<std::shared_mutex> lock(regionMapMtx);
                regionMap.emplace(new_region_memory, region_ptr);
            }
        }
    }
}

void* GCMemoryAllocator::allocate_new_memory(size_t size) {
    if (enableInternalMemoryManager)
        return this->allocate_from_freelist(size);
    else
        return malloc(size);
}

void* GCMemoryAllocator::allocate_from_freelist(size_t size) {
    std::thread::id tid = std::this_thread::get_id();
    int pool_idx = std::hash<std::thread::id>()(tid) % poolCount;
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
    {
        std::shared_lock<std::shared_mutex> lock(this->smallRegionQueMtx);
        for (int i = 0; i < smallRegionQue.size(); i++) {
            smallRegionQue[i]->clearUnmarked();
        }
    }
    {
        std::shared_lock<std::shared_mutex> lock(this->mediumRegionQueMtx);
        for (int i = 0; i < mediumRegionQue.size(); i++) {
            mediumRegionQue[i]->clearUnmarked();
        }
    }
    // TODO: 四个都要canFree
    {
        {
            std::shared_lock<std::shared_mutex> lock(this->largeRegionQueMtx);
            for (int i = 0; i < largeRegionQue.size(); i++) {
                if (largeRegionQue[i]->canFree()) {
                    std::unique_lock<std::shared_mutex> lock2(this->regionMapMtx);
                    regionMap.erase(largeRegionQue[i]->getStartAddr());
                    largeRegionQue[i]->free();
                }
            }
        }
        {
            std::unique_lock<std::shared_mutex> lock(this->largeRegionQueMtx);
            for (auto it = largeRegionQue.begin(); it != largeRegionQue.end(); it++) {
                if (it->get()->canFree()) {
                    it = largeRegionQue.erase(it);
                }
            }
        }
    }
    {
        std::shared_lock<std::shared_mutex> lock(this->tinyRegionQueMtx);
        for (int i = 0; i < tinyRegionQue.size(); i++) {
            tinyRegionQue[i]->clearUnmarked();
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
        return it->second;
    }
}
