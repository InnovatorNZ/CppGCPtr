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
#include "IAllocatable.h"
#include "GCRegion.h"
#include "GCMemoryManager.h"
#include "ConcurrentLinkedList.h"


class GCMemoryAllocator : public IAllocatable {
private:
    static const size_t INITIAL_SINGLE_SIZE;
    static const bool useConcurrentLinkedList;
    // GCMemoryManager memoryManager;
    bool enableInternalMemoryManager;
    unsigned int poolCount;
    std::vector<GCMemoryManager> memoryPools;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> smallRegionSet;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> mediumRegionSet;
    // std::unordered_set<GCRegion, GCRegion::GCRegionHash> largeRegionSet;
    // TODO: 能否使用无锁链表管理region？
// #if USE_CONCURRENT_LINKEDLIST
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> smallRegionList;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> mediumRegionList;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> largeRegionList;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> tinyRegionList;
// #else
    std::deque<std::shared_ptr<GCRegion>> smallRegionQue;
    std::deque<std::shared_ptr<GCRegion>> mediumRegionQue;
    std::deque<std::shared_ptr<GCRegion>> largeRegionQue;
    std::deque<std::shared_ptr<GCRegion>> tinyRegionQue;
    std::shared_mutex smallRegionQueMtx;
    std::shared_mutex mediumRegionQueMtx;
    std::shared_mutex largeRegionQueMtx;
    std::shared_mutex tinyRegionQueMtx;
// #endif
    std::map<void*, std::shared_ptr<GCRegion>> regionMap;
    std::shared_mutex regionMapMtx;

    void* allocate_from_region(size_t size, RegionEnum regionType);

    void* tryAllocateFromExistingRegion(size_t, ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void* tryAllocateFromExistingRegion(size_t, std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void allocate_new_region(RegionEnum regionType);

    void allocate_new_region(RegionEnum regionType, size_t regionSize);

    void* allocate_new_memory(size_t size);

    void* allocate_from_freelist(size_t size);

    void clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void relocateRegion(const std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void relocateRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

public:
    GCMemoryAllocator();

    explicit GCMemoryAllocator(bool useInternalMemoryManager);

    void* allocate(size_t size) override;

    void free(void*, size_t) override;

    void triggerClear();

    void triggerRelocation();

    std::shared_ptr<GCRegion> getRegion(void* object_addr);

    void resetLiveSize();
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
