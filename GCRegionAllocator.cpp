#include "GCRegionAllocator.h"

GCRegionAllocator::GCRegionAllocator() {
    this->poolCount = std::thread::hardware_concurrency();
    size_t initialSize = INITIAL_SINGLE_SIZE * poolCount;
    void* initialMemory = malloc(initialSize);
    for (int i = 0; i < poolCount; i++) {
        void* c_address = reinterpret_cast<void*>(reinterpret_cast<char*>(initialMemory) + i * INITIAL_SINGLE_SIZE);
        this->memoryPools.emplace_back(c_address, INITIAL_SINGLE_SIZE);
    }
}

GCRegion GCRegionAllocator::allocate(size_t size) {
    std::thread::id tid = std::this_thread::get_id();
    int pool_idx = std::hash<std::thread::id>()(tid) % poolCount;
    if (size < SMALL_REGION_OBJECT_THRESHOLD) {     // small region
        // TODO: 是否要将小中大分为三个memoryPool？答：还是算了
        void* start_address = this->allocate_memory(pool_idx, size);
        GCRegion region(42, RegionEnum::SMALL, start_address, size);
        return region;
    } else if (size < MEDIUM_REGION_OBJECT_THRESHOLD) {     // medium region

    } else {       // large region

    }

}

void* GCRegionAllocator::allocate_memory(int pool_idx, size_t size) {
    // TODO: 优先从threadLocal的memoryPool分配，若空间不足从别的steal过来，还不够则触发malloc并分配到memoryPool里
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