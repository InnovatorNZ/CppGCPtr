#include "GCMemoryAllocator.h"

#define max(a, b)            (((a) > (b)) ? (a) : (b))
#define min(a, b)            (((a) < (b)) ? (a) : (b))

const size_t GCMemoryAllocator::INITIAL_SINGLE_SIZE = 8 * 1024 * 1024;
thread_local std::shared_ptr<GCRegion> GCMemoryAllocator::smallAllocatingRegion;

GCMemoryAllocator::GCMemoryAllocator(bool useInternalMemoryManager, bool enableParallelClear,
                                     int gcThreadCount, ThreadPoolExecutor* gcThreadPool) {
    this->enableInternalMemoryManager = useInternalMemoryManager;
    this->enableParallelClear = enableParallelClear;
    this->gcThreadCount = gcThreadCount;
    this->threadPool = gcThreadPool;
    if constexpr (GCParameter::enableHashPool)
        this->poolCount = std::thread::hardware_concurrency();
    else
        this->poolCount = 1;
    if (useInternalMemoryManager) {
        size_t initialSize = INITIAL_SINGLE_SIZE * poolCount;
        void* initialMemory = malloc(initialSize);
        for (int i = 0; i < poolCount; i++) {
            void* c_address = reinterpret_cast<void*>(reinterpret_cast<char*>(initialMemory) + i * INITIAL_SINGLE_SIZE);
            this->memoryPools.emplace_back(c_address, INITIAL_SINGLE_SIZE);
        }
    }
    // this->smallAllocatingRegions = std::make_unique<std::atomic<std::shared_ptr<GCRegion>>[]>(poolCount);
    if constexpr (useConcurrentLinkedList) {
        this->smallRegionLists = std::make_unique<ConcurrentLinkedList<std::shared_ptr<GCRegion>>[]>(poolCount);
    } else {
        this->smallRegionQues = std::make_unique<std::deque<std::shared_ptr<GCRegion>>[]>(poolCount);
        this->smallRegionQueMtxs = std::make_unique<std::shared_mutex[]>(poolCount);
    }
    this->regionMapBuffer0.resize(poolCount);
    this->regionMapBuffer1.resize(poolCount);
    this->regionMapBufMtx0 = std::make_unique<std::mutex[]>(poolCount);
    this->regionMapBufMtx1 = std::make_unique<std::mutex[]>(poolCount);
}

std::pair<void*, std::shared_ptr<GCRegion>> GCMemoryAllocator::allocate(size_t size) {
    if (size <= GCRegion::TINY_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::TINY);
    } else if (size <= GCRegion::SMALL_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::SMALL);
    } else if (size <= GCRegion::MEDIUM_OBJECT_THRESHOLD) {
        return this->allocate_from_region(size, RegionEnum::MEDIUM);
    } else {
        return this->allocate_from_region(size, RegionEnum::LARGE);
    }
}

std::pair<void*, std::shared_ptr<GCRegion>> GCMemoryAllocator::allocate_from_region(size_t size, RegionEnum regionType) {
    if (size == 0) return std::make_pair(nullptr, nullptr);
    // thread_local std::shared_ptr<GCRegion> smallAllocatingRegion;
    while (true) {
        // 从已有region中寻找空闲区域
        std::shared_ptr<GCRegion> region;

        switch (regionType) {
            case RegionEnum::SMALL: {
                // 尝试从线程所属pool拿
                if (smallAllocatingRegion != nullptr) {
                    void* addr = smallAllocatingRegion->allocate(size);
                    if (addr != nullptr) return std::make_pair(addr, smallAllocatingRegion);
                }
#if 0
                int pool_idx = getPoolIdx();
                region = this->smallAllocatingRegions[pool_idx].load();
                if (region != nullptr) {
                    void* addr = region->allocate(size);
                    if (addr != nullptr) return std::make_pair(addr, region);
                }
                // 当前线程的pool没有，尝试从别的线程的拿
                for (int i = 0; i < poolCount; i++) {
                    std::shared_ptr<GCRegion> region_ = this->smallAllocatingRegions[i].load();
                    if (region_ != nullptr) {
                        void* addr = region_->allocate(size);
                        if (addr != nullptr) return std::make_pair(addr, region_);
                    }
                }
#endif
            }
                break;
            case RegionEnum::MEDIUM:
                region = this->mediumAllocatingRegion.load();
                if (region != nullptr) {
                    void* addr = region->allocate(size);
                    if (addr != nullptr) return std::make_pair(addr, region);
                }
                break;
            case RegionEnum::TINY:
                region = this->tinyAllocatingRegion.load();
                if (region != nullptr) {
                    void* addr = region->allocate(size);
                    if (addr != nullptr) return std::make_pair(addr, region);
                }
                break;
            case RegionEnum::LARGE:
                // 分配Large的时候不会从已有region里找，直接分配新的一块region
                break;
        }

        // 当前region不够，尝试分配新region
        size_t regionSize;
        switch (regionType) {
            case RegionEnum::SMALL:
                regionSize = GCRegion::SMALL_REGION_SIZE;
                break;
            case RegionEnum::MEDIUM:
                regionSize = GCRegion::MEDIUM_REGION_SIZE;
                break;
            case RegionEnum::TINY:
                regionSize = GCRegion::TINY_REGION_SIZE;
                break;
            default:
                regionSize = size;
                break;
        }

        void* new_region_memory = this->allocate_new_memory(regionSize);
        std::shared_ptr<GCRegion> new_region = std::make_shared<GCRegion>(regionType, new_region_memory, regionSize);

        std::unique_lock<std::shared_mutex> region_map_lock(regionMapMtx, std::defer_lock);
        if (regionType != RegionEnum::SMALL) region_map_lock.lock();

        switch (regionType) {
            case RegionEnum::SMALL: {
                smallAllocatingRegion = new_region;

                int pool_idx = getPoolIdx();
                if constexpr (enableRegionMapBuffer) {
                    if (region_map_lock.try_lock()) {
                        regionMap.emplace(new_region->getStartAddr(), new_region.get());
                        region_map_lock.unlock();
                    } else {
                        // 若获取region红黑树的锁失败，则将新region放入缓冲区内，在GC开始的时候再添加进红黑树
                        // 在此期间所有在新区域中的对象由于不在管理区域内会被误判定为gc root，不过问题不大
                        // 更新：如果支持调用析构函数的话那是问题不大，但是现在尚未支持，因此非root被释放后其仍然留存在rootset中，产生访问非法内存错误
                        // 再更新：即便支持调用析构函数也不行，因为root_set里存放的是指向gcptr的裸指针，并不会有指针自愈的功能，因而对象被转移后依然无法访问原有内存
                        // 考虑增加移动构造函数的支持，这样就会在转移的时候调用移动构造，也不会有上述问题了；
                        // 另外，如果不使用regionMapBuffer方案，需要在新region的CAS之前就上regionMapMtx锁，防止小概率线程不安全导致上述问题
                        std::clog << "Acquire region map mutex failed, adding to region map buffer" << std::endl;
                        while (true) {
                            if (regionMapBufMtx0[pool_idx].try_lock()) {
                                regionMapBuffer0[pool_idx].push_back(new_region.get());
                                regionMapBufMtx0[pool_idx].unlock();
                                break;
                            } else if (regionMapBufMtx1[pool_idx].try_lock()) {
                                regionMapBuffer1[pool_idx].push_back(new_region.get());
                                regionMapBufMtx1[pool_idx].unlock();
                                break;
                            }
                        }
                    }
                } else {
                    region_map_lock.lock();
                    regionMap.emplace(new_region->getStartAddr(), new_region.get());
                    region_map_lock.unlock();
                }

                if constexpr (useConcurrentLinkedList) {
                    smallRegionLists[pool_idx].push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[pool_idx]);
                    smallRegionQues[pool_idx].emplace_back(new_region);
                }

#if 0
                bool cas_success;
                if constexpr (enableRegionMapBuffer) {
                    cas_success = smallAllocatingRegions[pool_idx].compare_exchange_strong(region, new_region);
                } else {
                    if (smallAllocatingRegions[pool_idx].load(std::memory_order_acquire) == region) {
                        smallAllocatingRegions[pool_idx].store(new_region, std::memory_order_release);
                        regionMap.emplace(new_region->getStartAddr(), new_region.get());
                        cas_success = true;
                    } else {
                        cas_success = false;
                    }
                    region_map_lock.unlock();
                }
                if (cas_success) {
                    if constexpr (useConcurrentLinkedList) {
                        smallRegionLists[pool_idx].push_head(new_region);
                    } else {
                        std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[pool_idx]);
                        smallRegionQues[pool_idx].emplace_back(new_region);
                    }
                    if constexpr (enableRegionMapBuffer) {
                        if (region_map_lock.try_lock()) {
                            regionMap.emplace(new_region->getStartAddr(), new_region.get());
                            region_map_lock.unlock();
                        } else {
                            // 若获取region红黑树的锁失败，则将新region放入缓冲区内，在GC开始的时候再添加进红黑树
                            // 在此期间所有在新区域中的对象由于不在管理区域内会被误判定为gc root，不过问题不大
                            // 更新：如果支持调用析构函数的话那是问题不大，但是现在尚未支持，因此非root被释放后其仍然留存在rootset中，产生访问非法内存错误
                            // 再更新：即便支持调用析构函数也不行，因为root_set里存放的是指向gcptr的裸指针，并不会有指针自愈的功能，因而对象被转移后依然无法访问原有内存
                            // 考虑增加移动构造函数的支持，这样就会在转移的时候调用移动构造，也不会有上述问题了；
                            // 另外，如果不使用regionMapBuffer方案，需要在新region的CAS之前就上regionMapMtx锁，防止小概率线程不安全导致上述问题
                            std::clog << "Acquire region map mutex failed, adding to region map buffer" << std::endl;
                            while (true) {
                                if (regionMapBufMtx0[pool_idx].try_lock()) {
                                    regionMapBuffer0[pool_idx].push_back(new_region.get());
                                    regionMapBufMtx0[pool_idx].unlock();
                                    break;
                                } else if (regionMapBufMtx1[pool_idx].try_lock()) {
                                    regionMapBuffer1[pool_idx].push_back(new_region.get());
                                    regionMapBufMtx1[pool_idx].unlock();
                                    break;
                                }
                            }
                        }
                    }
                } else {
                    std::clog << "Undo allocating new region as someone beats us." << std::endl;
                    new_region->free();
                }
#endif
            }

                break;
            case RegionEnum::MEDIUM:
                if (mediumAllocatingRegion.load(std::memory_order_acquire) == region) {
                    mediumAllocatingRegion.store(new_region, std::memory_order_release);
                    regionMap.emplace(new_region->getStartAddr(), new_region.get());
                    region_map_lock.unlock();
                    if constexpr (useConcurrentLinkedList) {
                        mediumRegionList.push_head(new_region);
                    } else {
                        std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                        mediumRegionQue.emplace_back(new_region);
                    }
                } else {
                    region_map_lock.unlock();
                    new_region->free();
                }

                break;
            case RegionEnum::TINY:
                if (tinyAllocatingRegion.load(std::memory_order_acquire) == region) {
                    tinyAllocatingRegion.store(new_region, std::memory_order_release);
                    regionMap.emplace(new_region->getStartAddr(), new_region.get());
                    region_map_lock.unlock();
                    if constexpr (useConcurrentLinkedList) {
                        tinyRegionList.push_head(new_region);
                    } else {
                        std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                        tinyRegionQue.emplace_back(new_region);
                    }
                } else {
                    region_map_lock.unlock();
                    new_region->free();
                }

                break;
            case RegionEnum::LARGE:
                regionMap.emplace(new_region->getStartAddr(), new_region.get());
                region_map_lock.unlock();
                if constexpr (useConcurrentLinkedList) {
                    largeRegionList.push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(largeRegionQueMtx);
                    largeRegionQue.emplace_back(new_region);
                }

                return std::make_pair(new_region_memory, new_region);
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
    int pool_idx = getPoolIdx();
    // 优先从threadLocal的memoryPool分配，若空间不足从别的steal过来，还不够则触发malloc并分配到memoryPool里
    void* address = memoryPools[pool_idx].allocate(size);
    if (address != nullptr) return address;
    for (int i = 0; i < poolCount; i++) {
        address = memoryPools[i].allocate(size);
        if (address != nullptr) return address;
    }
    do {
        size_t malloc_size = max(INITIAL_SINGLE_SIZE, size);
        void* new_memory = malloc(malloc_size);
        memoryPools[pool_idx].free(new_memory, malloc_size);
        address = memoryPools[pool_idx].allocate(size);
        std::clog << "Allocating more memory from OS" << std::endl;
    } while (address == nullptr);
    return address;
}

void GCMemoryAllocator::triggerClear() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger clear." << std::endl;
        return;
    }
    /* triggerClear() 分三步：
     * 1. 判定clear条件，即GCRegion::canFree()，符合条件者执行GCRegion::setEvacuated()
     * 2. 执行clearUnmarked()（如有必要）
     * 3. 执行clearFreeRegion()，从容器中删除
     */
    // SelectClearSet();
    if constexpr (useConcurrentLinkedList) {
        // TODO：链表版本的
        throw std::invalid_argument("linked list doesn't support clear yet.");

        {
            for (int i = 0; i < poolCount; i++) {
                std::shared_lock<std::shared_mutex> lock(this->smallRegionQueMtxs[i]);
                for (int j = 0; j < smallRegionQues[i].size(); j++) {
                    smallRegionQues[i][j]->clearUnmarked();
                }
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(this->mediumRegionQueMtx);
            for (int i = 0; i < mediumRegionQue.size(); i++) {
                mediumRegionQue[i]->clearUnmarked();
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(this->tinyRegionQueMtx);
            for (int i = 0; i < tinyRegionQue.size(); i++) {
                tinyRegionQue[i]->clearUnmarked();
            }
        }


        clearFreeRegion(this->largeRegionList);
        for (int i = 0; i < poolCount; i++)
            clearFreeRegion(this->smallRegionLists[i]);
        clearFreeRegion(this->mediumRegionList);
        clearFreeRegion(this->tinyRegionList);
    } else {
        if constexpr (immediateClear || GCParameter::enableDestructorSupport) {
        // if constexpr (false) {      // TODO: 调查此处的bug？
            // 若启用析构函数，则强制每轮回收后都执行clearUnmarked()，不然会由于M0/M1重复导致被误判存活而不调用析构函数
            // 调用clearUnmarked可按region并行化
            const int PARALLEL_THRESHOLD = 16;
            {
                for (int i = 0; i < poolCount; i++) {
                    std::shared_lock<std::shared_mutex> lock(this->smallRegionQueMtxs[i]);
                    if (enableParallelClear && smallRegionQues[i].size() >= PARALLEL_THRESHOLD) {
                        size_t snum = smallRegionQues[i].size() / gcThreadCount;
                        for (int tid = 0; tid < gcThreadCount; tid++) {
                            threadPool->execute([this, i, tid, snum] {
                                size_t startIndex = tid * snum;
                                size_t endIndex = (tid == gcThreadCount - 1) ? smallRegionQues[i].size() : (tid + 1) * snum;
                                for (size_t j = startIndex; j < endIndex; j++) {
                                    smallRegionQues[i][j]->clearUnmarked();
                                }
                            });
                        }
                        threadPool->waitForTaskComplete(gcThreadCount);
                    } else {
                        for (auto& region : smallRegionQues[i]) {
                            region->clearUnmarked();
                        }
                    }
                }
            }
            {
                std::shared_lock<std::shared_mutex> lock(this->mediumRegionQueMtx);
                if (enableParallelClear && mediumRegionQue.size() >= PARALLEL_THRESHOLD) {
                    size_t snum = mediumRegionQue.size() / gcThreadCount;
                    for (int tid = 0; tid < gcThreadCount; tid++) {
                        threadPool->execute([this, tid, snum] {
                            size_t startIndex = tid * snum;
                            size_t endIndex = (tid == gcThreadCount - 1) ? mediumRegionQue.size() : (tid + 1) * snum;
                            for (size_t j = startIndex; j < endIndex; j++) {
                                mediumRegionQue[j]->clearUnmarked();
                            }
                        });
                    }
                    threadPool->waitForTaskComplete(gcThreadCount);
                } else {
                    for (auto& region : mediumRegionQue) {
                        region->clearUnmarked();
                    }
                }
            }
            {
                std::shared_lock<std::shared_mutex> lock(this->tinyRegionQueMtx);
                for (auto& region : tinyRegionQue) {
                    region->clearUnmarked();
                }
            }
        }
        // Step 3. 将clearQue中的所有region执行GCRegion::free()
        processClearQue();
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
    }
}

void GCMemoryAllocator::triggerRelocation(bool enableReclaim) {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger relocation." << std::endl;
        return;
    }

    Sleep(150);
    if (enableParallelClear) {
        size_t snum = evacuationQue.size() / gcThreadCount;
        for (int tid = 0; tid < gcThreadCount; tid++) {
            threadPool->execute([this, tid, snum] {
                size_t startIndex = tid * snum;
                size_t endIndex = (tid == gcThreadCount - 1) ? evacuationQue.size() : (tid + 1) * snum;
                for (size_t j = startIndex; j < endIndex; j++) {
                    evacuationQue[j]->triggerRelocation(this);
                }
            });
        }
        threadPool->waitForTaskComplete(gcThreadCount);

        if constexpr (immediateClear) {
            size_t snum = liveQue.size() / gcThreadCount;
            for (int tid = 0; tid < gcThreadCount; tid++) {
                threadPool->execute([this, tid, snum] {
                    size_t startIndex = tid * snum;
                    size_t endIndex = (tid == gcThreadCount - 1) ? liveQue.size() : (tid + 1) * snum;
                    for (size_t j = startIndex; j < endIndex; j++) {
                        liveQue[j]->clearUnmarked();
                    }
                });
            }
            threadPool->waitForTaskComplete(gcThreadCount);
        }
    } else {
        for (int i = 0; i < evacuationQue.size(); i++) {
            evacuationQue[i]->triggerRelocation(this);
        }
        if constexpr (immediateClear) {
            for (int i = 0; i < liveQue.size(); i++) {
                liveQue[i]->clearUnmarked();
            }
        }
    }

    if (enableReclaim) {
        throw std::invalid_argument("Reclaim is not implemented yet.");
#if 0
        // 小region按每poolIdx的数量成正比的概率分配
        std::vector<int> small_que_sizes;
        for (int i = 0; i < poolCount; i++) {
            if constexpr (!useConcurrentLinkedList) {
                small_que_sizes.push_back(smallRegionQues[i].size());
            } else {
                small_que_sizes.push_back(1);
            }
        }
        std::random_device rd;
        std::mt19937 gen(rd());
        std::discrete_distribution dist(small_que_sizes.begin(), small_que_sizes.end());

        for (auto& region : evacuationQue) {
            std::shared_ptr<GCRegion> inherit_region =
                std::make_shared<GCRegion>(std::move(*region));
            inherit_region->reclaim();
            switch (region->getRegionType()) {
                case RegionEnum::SMALL: {
                    int poolIdx = dist(gen);
                    {
                        std::unique_lock<std::mutex> lock(smallReclaimMtxs[poolIdx]);
                        smallReclaimQues[poolIdx].push_back(inherit_region);
                    }
                }
                                      break;
                case RegionEnum::MEDIUM: {
                    {
                        std::unique_lock<std::mutex> lock(mediumReclaimMtx);
                        mediumReclaimQue.push_back(inherit_region);
                    }
                    if constexpr (!useConcurrentLinkedList) {
                        std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                        mediumRegionQue.emplace_back(inherit_region);
                    } else {
                        mediumRegionList.push_head(inherit_region);
                    }
                }
                                       break;
                case RegionEnum::TINY: {
                    {
                        std::unique_lock<std::mutex> lock(tinyReclaimMtx);
                        tinyReclaimQue.push_back(inherit_region);
                    }
                    if constexpr (!useConcurrentLinkedList) {
                        std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                        tinyRegionQue.emplace_back(inherit_region);
                    } else {
                        tinyRegionList.push_head(inherit_region);
                    }
                }
                                     break;
            }
        }
#endif
    } else {
        if (enableParallelClear) {
            size_t snum = evacuationQue.size() / gcThreadCount;
            for (int tid = 0; tid < gcThreadCount; tid++) {
                threadPool->execute([this, tid, snum] {
                    size_t startIndex = tid * snum;
                    size_t endIndex = (tid == gcThreadCount - 1) ? evacuationQue.size() : (tid + 1) * snum;
                    for (size_t j = startIndex; j < endIndex; j++) {
                        evacuationQue[j]->free();
                    }
                });
            }
            threadPool->waitForTaskComplete(gcThreadCount);
        } else {
            for (auto& region : evacuationQue) {
                region->free();
            }
        }
    }

    if constexpr (useConcurrentLinkedList) {
        clearFreeRegion(this->largeRegionList);
    } else {
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
    }
}

void GCMemoryAllocator::triggerClear_v2() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger relocation." << std::endl;
        return;
    }

    if (enableParallelClear) {
        size_t snum = clearQue.size() / gcThreadCount;
        for (int tid = 0; tid < gcThreadCount; tid++) {
            threadPool->execute([this, tid, snum] {
                size_t startIndex = tid * snum;
                size_t endIndex = (tid == gcThreadCount - 1) ? clearQue.size() : (tid + 1) * snum;
                for (size_t j = startIndex; j < endIndex; j++) {
                    clearQue[j]->clearUnmarked();
                    clearQue[j]->free();
                }
            });
        }
        threadPool->waitForTaskComplete(gcThreadCount);
    } else {
        for (int i = 0; i < clearQue.size(); i++) {
            clearQue[i]->clearUnmarked();
            clearQue[i]->free();
        }
    }

    if constexpr (useConcurrentLinkedList) {
        clearFreeRegion(this->largeRegionList);
    } else {
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
    }

    clearQue.clear();
}

void GCMemoryAllocator::processClearQue() {
    removeClearedRegionMap();

    const int PARALLEL_THRESHOLD = 8;
    if (enableParallelClear && clearQue.size() > PARALLEL_THRESHOLD) {
        size_t snum = clearQue.size() / gcThreadCount;
        for (int tid = 0; tid < gcThreadCount; tid++) {
            threadPool->execute([this, tid, snum] {
                size_t startIndex = tid * snum;
                size_t endIndex = (tid == gcThreadCount - 1) ? clearQue.size() : (tid + 1) * snum;
                for (size_t j = startIndex; j < endIndex; j++) {
                    clearQue[j]->clearUnmarked();
                    clearQue[j]->free();
                }
            });
        }
        threadPool->waitForTaskComplete(gcThreadCount);
    } else {
        for (int i = 0; i < clearQue.size(); i++) {
            clearQue[i]->clearUnmarked();
            clearQue[i]->free();
        }
    }
    clearQue.clear();
}

void GCMemoryAllocator::SelectRelocationSet() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger select relocation set." << std::endl;
        return;
    }
    this->evacuationQue.clear();
    if constexpr (immediateClear) this->liveQue.clear();
    if constexpr (useConcurrentLinkedList) {
        for (int i = 0; i < poolCount; i++)
            selectRelocationSet(this->smallRegionLists[i]);
        selectRelocationSet(this->mediumRegionList);
        selectRelocationSet(this->tinyRegionList);
    } else {
        for (int i = 0; i < poolCount; i++)
            selectRelocationSet(smallRegionQues[i], smallRegionQueMtxs[i]);
        selectRelocationSet(mediumRegionQue, mediumRegionQueMtx);
        selectRelocationSet(tinyRegionQue, tinyRegionQueMtx);
    }
    removeEvacuatedRegionMap();
}

void GCMemoryAllocator::selectRelocationSet(std::deque<std::shared_ptr<GCRegion>>& regionQue,
                                            std::shared_mutex& regionQueMtx) {
    std::unique_lock<std::shared_mutex> lock(regionQueMtx);
    for (auto it = regionQue.begin(); it != regionQue.end();) {
        std::shared_ptr<GCRegion>& region = *it;
        if (!region->isEvacuated()) {
            if (region.use_count() == 1 || region->needEvacuate()) {
                region->setEvacuated();
                this->evacuationQue.emplace_back(std::move(region));
                it = regionQue.erase(it);
                continue;
            } else {
                if constexpr (immediateClear) {
                    this->liveQue.push_back(region.get());
                }
            }
        }
        ++it;
    }
}

void GCMemoryAllocator::selectRelocationSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        if (region != nullptr && !region->isEvacuated()) {
            if (region.use_count() <= 2 || region->needEvacuate()) {
                region->setEvacuated();
                this->evacuationQue.emplace_back(std::move(region));
                iterator->remove();
            } else if constexpr (immediateClear) {
                this->liveQue.push_back(region.get());
            }
        }
    }
}

void GCMemoryAllocator::SelectClearSet() {
    // this->clearQue.clear();
    if constexpr (useConcurrentLinkedList) {
        for (int i = 0; i < poolCount; i++)
            selectClearSet(this->smallRegionLists[i]);
        selectClearSet(this->mediumRegionList);
        selectClearSet(this->tinyRegionList);
    } else {
        for (int i = 0; i < poolCount; i++)
            selectClearSet(smallRegionQues[i], smallRegionQueMtxs[i]);
        selectClearSet(mediumRegionQue, mediumRegionQueMtx);
        selectClearSet(tinyRegionQue, tinyRegionQueMtx);
    }
    // removeClearedRegionMap();
}

void GCMemoryAllocator::selectClearSet(std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    std::unique_lock<std::shared_mutex> lock(regionQueMtx);
    for (auto it = regionQue.begin(); it != regionQue.end();) {
        std::shared_ptr<GCRegion>& region = *it;
        if (!region->isEvacuated()) {
            if (region.use_count() == 1 || region->canFree()) {
                region->setEvacuated();
                this->clearQue.emplace_back(std::move(region));
                it = regionQue.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void GCMemoryAllocator::selectClearSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        if (region != nullptr && !region->isEvacuated()) {
            if (region.use_count() <= 2 || region->canFree()) {
                region->setEvacuated();
                this->clearQue.emplace_back(std::move(region));
                iterator->remove();
            }
        }
    }
}

void GCMemoryAllocator::removeEvacuatedRegionMap() {
    std::unique_lock<std::shared_mutex> lock(regionMapMtx);
    for (auto& region : this->evacuationQue) {
        regionMap.erase(region->getStartAddr());
    }
}

void GCMemoryAllocator::removeClearedRegionMap() {
    std::unique_lock<std::shared_mutex> lock(regionMapMtx);
    for (auto& region : this->clearQue) {
        if (!regionMap.erase(region->getStartAddr()))
            std::clog << "Not removed from region map?" << std::endl;
    }
}

void GCMemoryAllocator::clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    {
        std::shared_lock<std::shared_mutex> lock(regionQueMtx);
        if (regionQue.empty()) return;

        const int PARALLEL_THRESHOLD = 64;
        if (!enableParallelClear || regionQue.size() < PARALLEL_THRESHOLD) {
            for (auto& region : regionQue) {
                if (region->canFree()) {
                    {
                        std::unique_lock<std::shared_mutex> lock2(regionMapMtx);
                        regionMap.erase(region->getStartAddr());
                    }
                    region->free();
                }
            }
        } else {
            size_t snum = regionQue.size() / gcThreadCount;
            for (int tid = 0; tid < gcThreadCount; tid++) {
                threadPool->execute([this, tid, snum, &regionQue] {
                    size_t startIndex = tid * snum;
                    size_t endIndex = (tid == gcThreadCount - 1) ? regionQue.size() : (tid + 1) * snum;
                    for (size_t j = startIndex; j < endIndex; j++) {
                        GCRegion* region = regionQue[j].get();
                        if (region->canFree()) {
                            {
                                std::unique_lock<std::shared_mutex> lock2(regionMapMtx);
                                regionMap.erase(region->getStartAddr());
                            }
                            region->free();
                        }
                    }
                });
            }
            threadPool->waitForTaskComplete(gcThreadCount);
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(regionQueMtx);
        for (auto it = regionQue.begin(); it != regionQue.end();) {
            GCRegion* region = it->get();
            if (region->canFree()) {
                it = regionQue.erase(it);
            } else {
                it++;
            }
        }
    }
}

void GCMemoryAllocator::clearFreeRegion(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        region->clearUnmarked();
        if (region->canFree()) {
            {
                std::unique_lock<std::shared_mutex> lock2(regionMapMtx);
                regionMap.erase(region->getStartAddr());
            }
            region->free();
            iterator->remove(region);
        }
    }
}

void GCMemoryAllocator::resetLiveSize() {
    if constexpr (useConcurrentLinkedList) {
        for (int i = 0; i < poolCount; i++) {
            auto iterator = smallRegionLists[i].getIterator();
            while (iterator->MoveNext()) {
                iterator->current()->resetLiveSize();
            }
        }
        for (auto regionList : { &mediumRegionList, &tinyRegionList }) {
            auto iterator = regionList->getIterator();
            while (iterator->MoveNext()) {
                iterator->current()->resetLiveSize();
            }
        }
    } else {
        {
            for (int i = 0; i < poolCount; i++) {
                std::shared_lock<std::shared_mutex> lock(smallRegionQueMtxs[i]);
                if (smallRegionQues[i].empty()) continue;
                const int PARALLEL_THREDSHOLD = 10000;
                if (!enableParallelClear || smallRegionQues[i].size() < PARALLEL_THREDSHOLD) {
                    for (auto& region : smallRegionQues[i]) {
                        region->resetLiveSize();
                    }
                } else {
                    size_t snum = smallRegionQues[i].size() / gcThreadCount;
                    for (int tid = 0; tid < gcThreadCount; tid++) {
                        threadPool->execute([this, i, tid, snum] {
                            size_t startIndex = tid * snum;
                            size_t endIndex = (tid == gcThreadCount - 1) ? smallRegionQues[i].size() : (tid + 1) * snum;
                            for (size_t j = startIndex; j < endIndex; j++) {
                                smallRegionQues[i][j]->resetLiveSize();
                            }
                        });
                    }
                    threadPool->waitForTaskComplete(gcThreadCount);
                }
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(mediumRegionQueMtx);
            for (auto& region : mediumRegionQue) {
                region->resetLiveSize();
            }
        }
        {
            std::shared_lock<std::shared_mutex> lock(tinyRegionQueMtx);
            for (auto& region : tinyRegionQue) {
                region->resetLiveSize();
            }
        }
    }
}

GCRegion* GCMemoryAllocator::queryRegionMap(void* object_addr) {
    std::shared_lock<std::shared_mutex> lock(this->regionMapMtx);
    auto it = regionMap.upper_bound(object_addr);
    if (it == regionMap.begin()) {
        return nullptr;
    } else {
        --it;
        return it->second;
    }
}

bool GCMemoryAllocator::inside_allocated_regions(void* object_addr) {
    GCRegion* region = queryRegionMap(object_addr);
    if (region == nullptr) {
        return false;
    } else {
        return region->inside_region(object_addr);
    }
}

void GCMemoryAllocator::flushRegionMapBuffer() {
    // 将缓冲区中的内容添加回regionMap
    for (int i = 0; i < poolCount; i++) {
        std::unique_lock<std::mutex> lock(regionMapBufMtx0[i]);
        if (regionMapBuffer0[i].empty()) continue;
        {
            std::unique_lock<std::shared_mutex> lock2(regionMapMtx);
            for (GCRegion* region : regionMapBuffer0[i]) {
                regionMap.emplace(region->getStartAddr(), region);
            }
        }
        regionMapBuffer0[i].clear();
    }
    for (int i = 0; i < poolCount; i++) {
        std::unique_lock<std::mutex> lock(regionMapBufMtx1[i]);
        if (regionMapBuffer1[i].empty()) continue;
        {
            std::unique_lock<std::shared_mutex> lock2(regionMapMtx);
            for (GCRegion* region : regionMapBuffer1[i]) {
                regionMap.emplace(region->getStartAddr(), region);
            }
        }
        regionMapBuffer1[i].clear();
    }
}

int GCMemoryAllocator::getPoolIdx() const {
    if (poolCount == 1) return 0;
    return GCUtil::getPoolIdx(poolCount);
}