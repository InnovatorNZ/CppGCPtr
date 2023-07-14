#include "GCMemoryAllocator.h"

const size_t GCMemoryAllocator::INITIAL_SINGLE_SIZE = 8 * 1024 * 1024;

GCMemoryAllocator::GCMemoryAllocator(bool useInternalMemoryManager, bool enableParallelClear,
                                     int gcThreadCount, ThreadPoolExecutor* gcThreadPool) {
    this->enableInternalMemoryManager = useInternalMemoryManager;
    this->enableParallelClear = enableParallelClear;
    this->gcThreadCount = gcThreadCount;
    this->threadPool = gcThreadPool;
    this->poolCount = std::thread::hardware_concurrency();
    if (useInternalMemoryManager) {
        size_t initialSize = INITIAL_SINGLE_SIZE * poolCount;
        void* initialMemory = malloc(initialSize);
        for (int i = 0; i < poolCount; i++) {
            void* c_address = reinterpret_cast<void*>(reinterpret_cast<char*>(initialMemory) + i * INITIAL_SINGLE_SIZE);
            this->memoryPools.emplace_back(c_address, INITIAL_SINGLE_SIZE);
        }
    }
    this->smallAllocatingRegions = std::make_unique<std::atomic<std::shared_ptr<GCRegion>>[]>(poolCount);
    this->smallReclaimQues.resize(poolCount);
    this->smallReclaimMtxs = std::make_unique<std::mutex[]>(poolCount);
    if constexpr (useConcurrentLinkedList) {
        this->smallRegionLists = std::make_unique<ConcurrentLinkedList<std::shared_ptr<GCRegion>>[]>(poolCount);
    } else {
        this->smallRegionQues = std::make_unique<std::deque<std::shared_ptr<GCRegion>>[]>(poolCount);
        this->smallRegionQueMtxs = std::make_unique<std::shared_mutex[]>(poolCount);
    }
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

std::pair<void*, std::shared_ptr<GCRegion>>
GCMemoryAllocator::tryAllocateFromExistingRegion(size_t size, ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        void* addr = region->allocate(size);
        if (addr != nullptr) return std::make_pair(addr, region);
    }
    return std::make_pair(nullptr, nullptr);
}

std::pair<void*, std::shared_ptr<GCRegion>>
GCMemoryAllocator::tryAllocateFromExistingRegion(size_t size, std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    std::shared_lock<std::shared_mutex> lock(regionQueMtx);
    for (int i = regionQue.size() - 1; i >= 0; i--) {
        std::shared_ptr<GCRegion>& region = regionQue[i];
        if (GCPhase::duringMarking() && region->needEvacuate()) continue;
        void* addr = region->allocate(size);
        if (addr != nullptr) return std::make_pair(addr, region);
    }
    return std::make_pair(nullptr, nullptr);
}

void GCMemoryAllocator::allocate_new_region(RegionEnum regionType) {
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
            regionSize = 0;
            break;
    }
    allocate_new_region(regionType, regionSize);
}

void GCMemoryAllocator::allocate_new_region(RegionEnum regionType, size_t regionSize) {
    // 为啥不能直接调用操作系统的malloc获取region的内存？为啥还要搞个全局freelist？
    void* new_region_memory = this->allocate_new_memory(regionSize);
    std::shared_ptr<GCRegion> region_ptr = std::make_shared<GCRegion>(regionType, new_region_memory, regionSize);

    switch (regionType) {
        case RegionEnum::SMALL: {
            int pool_idx = getPoolIdx();
            if constexpr (useConcurrentLinkedList) {
                smallRegionLists[pool_idx].push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[pool_idx]);
                smallRegionQues[pool_idx].emplace_back(region_ptr);
            }
        }
            break;
        case RegionEnum::MEDIUM:
            if constexpr (useConcurrentLinkedList) {
                mediumRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                mediumRegionQue.emplace_back(region_ptr);
            }
            break;
        case RegionEnum::TINY:
            if constexpr (useConcurrentLinkedList) {
                tinyRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                tinyRegionQue.emplace_back(region_ptr);
            }
            break;
        case RegionEnum::LARGE:
            if constexpr (useConcurrentLinkedList) {
                largeRegionList.push_head(region_ptr);
            } else {
                std::unique_lock<std::shared_mutex> lock(largeRegionQueMtx);
                largeRegionQue.emplace_back(region_ptr);
            }
            break;
    }
}

std::pair<void*, std::shared_ptr<GCRegion>> GCMemoryAllocator::allocate_from_region(size_t size, RegionEnum regionType) {
    if (size == 0) return std::make_pair(nullptr, nullptr);
    while (true) {
        // 从已有region中寻找空闲区域
        // std::pair<void*, std::shared_ptr<GCRegion>> ret{nullptr, nullptr};
        std::shared_ptr<GCRegion> region;
        if constexpr (useConcurrentLinkedList) {
            throw std::invalid_argument("allocate_from_region() is not implemented yet for concurrent linked list");
#if 0
            switch (regionType) {
                case RegionEnum::SMALL:
                    ret = this->tryAllocateFromExistingRegion(size, this->smallRegionLists[getPoolIdx()]);
                    if (ret.first != nullptr) return ret;
                    for (int i = 0; i < poolCount; i++) {
                        ret = this->tryAllocateFromExistingRegion(size, this->smallRegionLists[i]);
                        if (ret.first != nullptr) return ret;
                    }
                    break;
                case RegionEnum::MEDIUM:
                    ret = this->tryAllocateFromExistingRegion(size, this->mediumRegionList);
                    break;
                case RegionEnum::TINY:
                    ret = this->tryAllocateFromExistingRegion(size, this->tinyRegionList);
                    break;
                case RegionEnum::LARGE:
                    // 分配Large的时候不会从已有region里找，直接分配新的一块region
                    break;
            }
#endif
        } else {
            switch (regionType) {
                case RegionEnum::SMALL: {
                    // 尝试从线程所属pool拿
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
        }

        // 所有region都不够，尝试从reclaim队列中取一个可用的并安装上去
        if (regionType != RegionEnum::LARGE) {
            switch (regionType) {
                case RegionEnum::SMALL: {
                    int pool_idx = getPoolIdx();
                    std::unique_lock<std::mutex> lock(smallReclaimMtxs[pool_idx]);
                    if (!smallReclaimQues[pool_idx].empty()) {
                        std::shared_ptr<GCRegion>& new_region = smallReclaimQues[pool_idx].back();
                        if (smallAllocatingRegions[pool_idx].compare_exchange_weak(region, new_region)) {
                            smallReclaimQues[pool_idx].pop_back();
                        }
                        continue;
                    }
                }
                    break;
                case RegionEnum::MEDIUM: {
                    std::unique_lock<std::mutex> lock(mediumReclaimMtx);
                    if (!mediumReclaimQue.empty()) {
                        std::shared_ptr<GCRegion>& new_region = mediumReclaimQue.back();
                        if (mediumAllocatingRegion.compare_exchange_weak(region, new_region)) {
                            mediumReclaimQue.pop_back();
                        }
                        continue;
                    }
                }
                    break;
                case RegionEnum::TINY: {
                    std::unique_lock<std::mutex> lock(tinyReclaimMtx);
                    if (!tinyReclaimQue.empty()) {
                        std::shared_ptr<GCRegion>& new_region = tinyReclaimQue.back();
                        if (tinyAllocatingRegion.compare_exchange_weak(region, new_region)) {
                            tinyReclaimQue.pop_back();
                        }
                        continue;
                    }
                }
                    break;
            }
        }

        // 分配新region
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

        switch (regionType) {
            case RegionEnum::SMALL: {
                int pool_idx = getPoolIdx();
                if (!smallAllocatingRegions[pool_idx].compare_exchange_weak(region, new_region)) {
                    std::unique_lock<std::mutex> lock(smallReclaimMtxs[pool_idx]);
                    smallReclaimQues[pool_idx].push_back(new_region);
                }
                if constexpr (useConcurrentLinkedList) {
                    smallRegionLists[pool_idx].push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[pool_idx]);
                    smallRegionQues[pool_idx].emplace_back(new_region);
                }
            }
                break;
            case RegionEnum::MEDIUM:
                if (!mediumAllocatingRegion.compare_exchange_weak(region, new_region)) {
                    std::unique_lock<std::mutex> lock(mediumReclaimMtx);
                    mediumReclaimQue.push_back(new_region);
                }
                if constexpr (useConcurrentLinkedList) {
                    mediumRegionList.push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(mediumRegionQueMtx);
                    mediumRegionQue.emplace_back(new_region);
                }
                break;
            case RegionEnum::TINY:
                if (!tinyAllocatingRegion.compare_exchange_weak(region, new_region)) {
                    std::unique_lock<std::mutex> lock(tinyReclaimMtx);
                    tinyReclaimQue.push_back(new_region);
                }
                if constexpr (useConcurrentLinkedList) {
                    tinyRegionList.push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(tinyRegionQueMtx);
                    tinyRegionQue.emplace_back(new_region);
                }
                break;
            case RegionEnum::LARGE:
                if constexpr (useConcurrentLinkedList) {
                    largeRegionList.push_head(new_region);
                } else {
                    std::unique_lock<std::shared_mutex> lock(largeRegionQueMtx);
                    largeRegionQue.emplace_back(new_region);
                }
                break;
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
    if constexpr (useConcurrentLinkedList) {
        clearFreeRegion(this->largeRegionList);
        for (int i = 0; i < poolCount; i++)
            clearFreeRegion(this->smallRegionLists[i]);
        clearFreeRegion(this->mediumRegionList);
        clearFreeRegion(this->tinyRegionList);
    } else {
        // 调用clearUnmarked可按region并行化
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
        // clearFreeRegion可由四个线程并行化，但对于每种类型应单线程
        clearFreeRegion(largeRegionQue, largeRegionQueMtx);
        for (int i = 0; i < poolCount; i++)
            clearFreeRegion(smallRegionQues[i], smallRegionQueMtxs[i]);
        clearFreeRegion(mediumRegionQue, mediumRegionQueMtx);
        clearFreeRegion(tinyRegionQue, tinyRegionQueMtx);
    }
}

void GCMemoryAllocator::triggerRelocation(bool enableReclaim) {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger relocation." << std::endl;
        return;
    }
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
    } else {
        for (int i = 0; i < evacuationQue.size(); i++) {
            evacuationQue[i]->triggerRelocation(this);
        }
    }

    if (enableReclaim) {
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
                    if constexpr (!useConcurrentLinkedList) {
                        std::unique_lock<std::shared_mutex> lock(smallRegionQueMtxs[poolIdx]);
                        smallRegionQues[poolIdx].emplace_back(inherit_region);
                    } else {
                        smallRegionLists[poolIdx].push_head(inherit_region);
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

void GCMemoryAllocator::SelectRelocationSet() {
    if (GCPhase::getGCPhase() != eGCPhase::SWEEP) {
        std::cerr << "Wrong phase, should in sweeping phase to trigger select relocation set." << std::endl;
        return;
    }
    this->evacuationQue.clear();
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
}

void GCMemoryAllocator::selectRelocationSet(std::deque<std::shared_ptr<GCRegion>>& regionQue,
                                            std::shared_mutex& regionQueMtx) {
    std::unique_lock<std::shared_mutex> lock(regionQueMtx);
    for (auto it = regionQue.begin(); it != regionQue.end();) {
        std::shared_ptr<GCRegion>& region = *it;
        if (!region->isEvacuated() && region->needEvacuate()) {
            region->setEvacuated();
            this->evacuationQue.emplace_back(region);
            it = regionQue.erase(it);
        } else {
            it++;
        }
    }
}

void GCMemoryAllocator::selectRelocationSet(ConcurrentLinkedList<std::shared_ptr<GCRegion>>& regionList) {
    auto iterator = regionList.getRemovableIterator();
    while (iterator->MoveNext()) {
        std::shared_ptr<GCRegion> region = iterator->current();
        if (!region->isEvacuated() && region->needEvacuate()) {
            region->setEvacuated();
            this->evacuationQue.emplace_back(region);
            iterator->remove();
        }
    }
}

void GCMemoryAllocator::clearFreeRegion(std::deque<std::shared_ptr<GCRegion>>& regionQue, std::shared_mutex& regionQueMtx) {
    {
        std::shared_lock<std::shared_mutex> lock(regionQueMtx);
        if (!enableParallelClear) {
            for (auto& region : regionQue) {
                if (region->canFree()) {
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
                        if (regionQue[j]->canFree()) {
                            regionQue[j]->free();
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
            if (it->get()->canFree()) {
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
        for (auto regionList : {&mediumRegionList, &tinyRegionList}) {
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
                if (!enableParallelClear) {
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

int GCMemoryAllocator::getPoolIdx() const {
    std::thread::id tid = std::this_thread::get_id();
    int pool_idx = std::hash<std::thread::id>()(tid) % poolCount;
    return pool_idx;
}