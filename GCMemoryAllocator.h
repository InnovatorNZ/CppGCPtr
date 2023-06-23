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
#include "IMemoryAllocator.h"
#include "GCRegion.h"
#include "GCMemoryManager.h"
#include "ConcurrentLinkedList.h"


class GCMemoryAllocator : public IMemoryAllocator {
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
    // 能否使用无锁链表管理region？ TODO: 似乎使用链表管理region会导致多线程优化较为困难
// #if USE_CONCURRENT_LINKEDLIST
    std::unique_ptr<ConcurrentLinkedList<std::shared_ptr<GCRegion>>[]> smallRegionLists;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> mediumRegionList;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> largeRegionList;
    ConcurrentLinkedList<std::shared_ptr<GCRegion>> tinyRegionList;
// #else
    std::unique_ptr<std::deque<std::shared_ptr<GCRegion>>[]> smallRegionQues;
    std::deque<std::shared_ptr<GCRegion>> mediumRegionQue;
    std::deque<std::shared_ptr<GCRegion>> largeRegionQue;
    std::deque<std::shared_ptr<GCRegion>> tinyRegionQue;
    std::unique_ptr<std::shared_mutex[]> smallRegionQueMtxs;
    std::shared_mutex mediumRegionQueMtx;
    std::shared_mutex largeRegionQueMtx;
    std::shared_mutex tinyRegionQueMtx;
// #endif
    // std::map<void*, std::shared_ptr<GCRegion>> regionMap;
    // std::shared_mutex regionMapMtx;

    std::pair<void*, std::shared_ptr<GCRegion>> allocate_from_region(size_t size, RegionEnum regionType);

    std::pair<void*, std::shared_ptr<GCRegion>> tryAllocateFromExistingRegion(size_t, ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    std::pair<void*, std::shared_ptr<GCRegion>> tryAllocateFromExistingRegion(size_t, std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void allocate_new_region(RegionEnum regionType);

    void allocate_new_region(RegionEnum regionType, size_t regionSize);

    void* allocate_new_memory(size_t size);

    void* allocate_from_freelist(size_t size);

    void clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void relocateRegion(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void relocateRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    int getPoolIdx();

public:
    GCMemoryAllocator();

    explicit GCMemoryAllocator(bool useInternalMemoryManager);

    std::pair<void*, std::shared_ptr<GCRegion>> allocate(size_t size) override;

    // void free(void*, size_t, std::shared_ptr<GCRegion>) override;

    void triggerClear();

    void triggerRelocation();

    // std::shared_ptr<GCRegion> getRegion(void* object_addr);

    void resetLiveSize();
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
