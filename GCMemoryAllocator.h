#ifndef CPPGCPTR_GCREGIONALLOCATOR_H
#define CPPGCPTR_GCREGIONALLOCATOR_H

#include <vector>
#include <deque>
#include <unordered_set>
#include <map>
#include <thread>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include "GCRegion.h"
#include "GCMemoryManager.h"


class GCMemoryAllocator {
private:
    const size_t SMALL_REGION_OBJECT_THRESHOLD = 32 * 1024;
    const size_t SMALL_REGION_SIZE = 512 * 1024;
    const size_t MEDIUM_REGION_OBJECT_THRESHOLD = 1 * 1024 * 1024;
    const size_t MEDIUM_REGION_SIZE = 32 * 1024 * 1024;
    const size_t INITIAL_SINGLE_SIZE = 8 * 1024 * 1024;
    // GCMemoryManager memoryManager;
    bool enableInternalMemoryManager;
    unsigned int poolCount;
    std::vector<GCMemoryManager> memoryPools;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> smallRegionSet;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> mediumRegionSet;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> largeRegionSet;
    std::deque<std::shared_ptr<GCRegion>> smallRegionQue;
    std::deque<std::shared_ptr<GCRegion>> mediumRegionQue;
    std::deque<std::shared_ptr<GCRegion>> largeRegionQue;
    std::shared_mutex smallRegionQueMtx;
    std::shared_mutex mediumRegionQueMtx;
    std::shared_mutex largeRegionQueMtx;
    std::map<void*, std::shared_ptr<GCRegion>> regionMap;
    std::shared_mutex regionMapMtx;

    void* allocate_from_region(size_t size, RegionEnum regionType);

    void* allocate_new_memory(size_t size);

    void* allocate_from_freelist(size_t size);

public:
    GCMemoryAllocator();

    explicit GCMemoryAllocator(bool useInternalMemoryManager);

    void* allocate(size_t size);

    void free(GCRegion region) = delete;

    void triggerClear();
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
