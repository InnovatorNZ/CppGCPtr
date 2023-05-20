#include "GCRegionAllocator.h"

GCRegionAllocator::GCRegionAllocator() {
    this->poolCount = std::thread::hardware_concurrency();
    //GCMemoryManager manager((void*) 3, 3);
    //this->memoryPools.resize(poolCount, manager);
    for (int i = 0; i < poolCount; i++) {
        this->memoryPools.emplace_back((void*) 3, 3);
    }
}

GCRegion GCRegionAllocator::allocate(size_t size) {
    if (size < SMALL_REGION_OBJECT_THRESHOLD) {     // small region

    } else if (size < MEDIUM_REGION_OBJECT_THRESHOLD) {     // medium region

    } else {       // large region

    }
    
}