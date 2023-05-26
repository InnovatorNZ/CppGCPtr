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
    if (size < SMALL_REGION_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::SMALL);
    } else if (size < MEDIUM_REGION_OBJECT_THRESHOLD) {
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
                    (42, RegionEnum::SMALL, new_region_memory, SMALL_REGION_SIZE);
            {
                std::unique_lock<std::shared_mutex> lock(smallRegionQueMtx);
                smallRegionQue.emplace_back(region_ptr);
                //smallRegionQue.emplace_back(42, RegionEnum::SMALL, new_region_memory, SMALL_REGION_SIZE);
            }
            {
                std::unique_lock<std::shared_mutex> lock(regionMapMtx);
                regionMap.emplace(new_region_memory, region_ptr);
            }
        }
    } else if (regionType == RegionEnum::MEDIUM) {
        // TODO: MEDIUM

    } else {
        // TODO: LARGE

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
    {
        std::shared_lock<std::shared_mutex> lock(this->largeRegionQueMtx);
        for (int i = 0; i < largeRegionQue.size(); i++) {
            largeRegionQue[i]->clearUnmarked();
        }
    }
}
