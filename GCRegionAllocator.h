#ifndef CPPGCPTR_GCREGIONALLOCATOR_H
#define CPPGCPTR_GCREGIONALLOCATOR_H

#include <vector>
#include <unordered_set>
#include <thread>
#include "GCRegion.h"
#include "GCMemoryManager.h"


class GCRegionAllocator {
private:
    const int SMALL_REGION_OBJECT_THRESHOLD = 32 * 1024;
    const int SMALL_REGION_SIZE = 512 * 1024;
    const int MEDIUM_REGION_OBJECT_THRESHOLD = 1 * 1024 * 1024;
    const int MEDIUM_REGION_SIZE = 32 * 1024 * 1024;
    //GCMemoryManager memoryManager;
    int poolCount;
    std::vector<GCMemoryManager> memoryPools;
    std::unordered_set<GCRegion, GCRegion::GCRegionHash> regionSet;
public:
    GCRegionAllocator();

    GCRegion allocate(size_t size);

    void free(GCRegion region);
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
