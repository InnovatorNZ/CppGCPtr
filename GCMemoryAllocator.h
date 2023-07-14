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
#include <random>
#include "IMemoryAllocator.h"
#include "GCRegion.h"
#include "GCMemoryManager.h"
#include "ConcurrentLinkedList.h"
#include "CppExecutor/ThreadPoolExecutor.h"

class GCMemoryAllocator : public IMemoryAllocator {
private:
    static const size_t INITIAL_SINGLE_SIZE;
    static constexpr bool useConcurrentLinkedList = false;
    bool enableInternalMemoryManager;
    bool enableParallelClear;
    unsigned int gcThreadCount;
    unsigned int poolCount;
    std::vector<GCMemoryManager> memoryPools;
    ThreadPoolExecutor* threadPool;
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
    std::unique_ptr<std::atomic<std::shared_ptr<GCRegion>>[]> smallAllocatingRegions;
    std::atomic<std::shared_ptr<GCRegion>> mediumAllocatingRegion;
    std::atomic<std::shared_ptr<GCRegion>> tinyAllocatingRegion;
    std::vector<std::shared_ptr<GCRegion>> evacuationQue;

    std::pair<void*, std::shared_ptr<GCRegion>> allocate_from_region(size_t size, RegionEnum regionType);

    std::pair<void*, std::shared_ptr<GCRegion>> tryAllocateFromExistingRegion(size_t, ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    std::pair<void*, std::shared_ptr<GCRegion>> tryAllocateFromExistingRegion(size_t, std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void allocate_new_region(RegionEnum regionType);

    void allocate_new_region(RegionEnum regionType, size_t regionSize);

    void* allocate_new_memory(size_t size);

    void* allocate_from_freelist(size_t size);

    void clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void selectRelocationSet(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void selectRelocationSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    int getPoolIdx() const;

public:
    GCMemoryAllocator(bool useInternalMemoryManager = false, bool enableParallelClear = false,
                      int gcThreadCount = 0, ThreadPoolExecutor* = nullptr);

    std::pair<void*, std::shared_ptr<GCRegion>> allocate(size_t size) override;

    void triggerClear();

    void SelectRelocationSet();

    void triggerRelocation(bool enableReclaim = false);

    void resetLiveSize();
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
