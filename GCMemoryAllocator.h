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
#include "GCParameter.h"
#include "GCRegion.h"
#include "GCMemoryManager.h"
#include "GCUtil.h"
#include "ConcurrentLinkedList.h"
#include "CppExecutor/ThreadPoolExecutor.h"

class GCRegion;

enum class RegionEnum;

class GCMemoryAllocator : public IMemoryAllocator {
private:
    static constexpr bool useConcurrentLinkedList = GCParameter::useConcurrentLinkedList;
    static constexpr bool enableRegionMapBuffer =
            GCParameter::enableRegionMapBuffer && GCParameter::enableMoveConstructor && GCParameter::enableDestructorSupport;
    static constexpr bool immediateClear = GCParameter::immediateClear;
    bool enableInternalMemoryManager;
    bool enableParallelClear;
    unsigned int gcThreadCount;
    unsigned int poolCount;
    std::vector<GCMemoryManager> memoryPools;
    ThreadPoolExecutor* threadPool;
    // 能否使用无锁链表管理region？似乎使用链表管理region会导致多线程优化较为困难
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
    static thread_local std::shared_ptr<GCRegion> smallAllocatingRegion;
    static thread_local std::shared_ptr<GCRegion> smallRelocatingRegion;
    std::atomic<std::shared_ptr<GCRegion>> mediumAllocatingRegion;
    std::atomic<std::shared_ptr<GCRegion>> tinyAllocatingRegion;

    std::vector<std::shared_ptr<GCRegion>> evacuationQue;
    std::vector<std::shared_ptr<GCRegion>> clearQue;
    std::vector<GCRegion*> liveQue;
    // 用于判定gc root，是否在被管理区域内的红黑树
    std::map<void*, GCRegion*> regionMap;
    std::shared_mutex regionMapMtx;
    std::vector<std::vector<GCRegion*>> regionMapBuffer0, regionMapBuffer1;
    std::unique_ptr<std::mutex[]> regionMapBufMtx0, regionMapBufMtx1;

    std::pair<void*, std::shared_ptr<GCRegion>>
        allocate_from_region(size_t size, RegionEnum regionType, bool relocate = false);

    void* allocate_new_memory(size_t size);

    void* allocate_from_freelist(size_t size);

    void clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void selectRelocationSet(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void selectRelocationSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void selectClearSet(std::deque<std::shared_ptr<GCRegion>>&, std::shared_mutex&);

    void selectClearSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>&);

    void removeEvacuatedRegionMap();

    void removeClearedRegionMap();

    void processClearQue();

    int getPoolIdx() const;

    GCRegion* queryRegionMap(void*);

public:
    GCMemoryAllocator(bool useInternalMemoryManager = false, bool enableParallelClear = false,
                      int gcThreadCount = 0, ThreadPoolExecutor* = nullptr);

    GCMemoryAllocator(const GCMemoryAllocator&) = delete;

    GCMemoryAllocator(GCMemoryAllocator&&) noexcept = delete;

    std::pair<void*, std::shared_ptr<GCRegion>> allocate(size_t size) override;

    std::pair<void*, std::shared_ptr<GCRegion>> relocate(size_t size) override;

    void* allocate_raw(size_t) override;

    void free(void*, size_t) override;

    void triggerRelocation();

    void triggerClear();

    void SelectRelocationSet();

    void SelectClearSet();

    void resetLiveSize();

    bool inside_allocated_regions(void*);

    void flushRegionMapBuffer();

    void freeReservedMemory();
};


#endif //CPPGCPTR_GCREGIONALLOCATOR_H
