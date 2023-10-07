#include "GCWorker.h"

std::unique_ptr<GCWorker> GCWorker::instance;

GCWorker::GCWorker() : GCWorker(false, false, true, false, false, false) {
}

GCWorker::GCWorker(bool concurrent, bool enableMemoryAllocator, bool enableDestructorSupport, bool useInlineMarkState,
                   bool useSecondaryMemoryManager, bool enableRelocation, bool enableParallel, bool enableReclaim) :
        stop_(false), ready_(false), enableConcurrentMark(concurrent), enableMemoryAllocator(enableMemoryAllocator) {
    std::clog << "GCWorker()" << std::endl;
    if (enableReclaim || useSecondaryMemoryManager) {
        // TODO: 重利用和空闲列表二级内存分配器尚未实现
        throw std::logic_error("Reclaim and secondary memory manager is not prepared yet. Please contact developer");
    }
    if (!enableMemoryAllocator) {
        enableParallel = false;             // 必须启用内存分配器以支持并行垃圾回收
        enableRelocation = false;           // 必须启用内存分配器以支持移动式回收
        useSecondaryMemoryManager = false;
        enableReclaim = false;
    }
    this->enableParallelGC = enableParallel;
    this->enableRelocation = enableRelocation;
    this->enableDestructorSupport = enableDestructorSupport;
    this->enableReclaim = enableReclaim;
    if (enableRelocation) useInlineMarkstate = true;
    this->useInlineMarkstate = useInlineMarkState;

    if constexpr (GCParameter::enableHashPool)
        this->poolCount = std::thread::hardware_concurrency();
    else
        this->poolCount = 1;
    this->satb_queue_pool.resize(poolCount);
    this->satb_queue_pool_mutex = std::make_unique<std::mutex[]>(poolCount);
    if constexpr (GCParameter::deferRemoveRoot) {
        root_map = std::make_unique<std::unordered_map<GCPtrBase*, bool>[]>(poolCount);
        for (int i = 0; i < poolCount; i++)
            root_map[i].reserve(64);
    } else {
        root_set = std::make_unique<std::unordered_set<GCPtrBase*>[]>(poolCount);
        for (int i = 0; i < poolCount; i++)
            root_set[i].reserve(64);
    }
    root_set_mutex = std::make_unique<std::shared_mutex[]>(poolCount);
    if (enableParallel) {
        this->gcThreadCount = 4;
        this->threadPool = std::make_unique<ThreadPoolExecutor>(gcThreadCount, gcThreadCount, 0,
                                                                std::make_unique<ArrayBlockingQueue<std::function<void()>>>(4),
                                                                ThreadPoolExecutor::AbortPolicy);
    } else {
        this->gcThreadCount = 0;
        this->threadPool = nullptr;
    }
    if (enableMemoryAllocator) {
        if (enableParallel)
            this->memoryAllocator = std::make_unique<GCMemoryAllocator>(useSecondaryMemoryManager, true, gcThreadCount, threadPool.get());
        else
            this->memoryAllocator = std::make_unique<GCMemoryAllocator>(useSecondaryMemoryManager);
    }
    if (concurrent) {
        this->gc_thread = std::make_unique<std::thread>(&GCWorker::GCThreadLoop, this);
    } else {
        this->gc_thread = nullptr;
    }
}

GCWorker::~GCWorker() {
    std::clog << "~GCWorker()" << std::endl;
    {
        std::unique_lock<std::mutex> lock(this->thread_mutex);
        stop_ = true;
        ready_ = true;
    }
    condition.notify_all();
    if (gc_thread != nullptr)
        gc_thread->join();
}

GCWorker* GCWorker::getWorker() {
    if (instance == nullptr) {
        static std::mutex singleton_mutex;
        std::unique_lock<std::mutex> lock(singleton_mutex);
        if (instance == nullptr) {
            GCWorker* pGCWorker = new GCWorker
                    (GCParameter::enableConcurrentGC, GCParameter::enableMemoryAllocator, GCParameter::enableDestructorSupport,
                     GCParameter::useInlineMarkState, GCParameter::useSecondaryMemoryManager, GCParameter::enableRelocation,
                     GCParameter::enableParallelGC, GCParameter::enableReclaim);
            instance = std::unique_ptr<GCWorker>(pGCWorker);
        }
    }
    return instance.get();
}

void GCWorker::mark(void* object_addr) {
    if (object_addr == nullptr) return;
    std::shared_lock<std::shared_mutex> read_lock(this->object_map_mutex);
    auto it = object_map.find(object_addr);
    if (it == object_map.end()) {
        std::clog << "Object not found at " << object_addr << std::endl;
        return;
    }
    read_lock.unlock();
    MarkState c_markstate = GCPhase::getCurrentMarkState();
    if (c_markstate == it->second.markState)    // 标记过了
        return;
    it->second.markState = c_markstate;
    size_t object_size = it->second.objectSize;
    char* cptr = reinterpret_cast<char*>(object_addr);
    for (char* n_addr = cptr; n_addr < cptr + object_size - sizeof(void*) * 2; n_addr += sizeof(void*)) {
        int identifier = *(reinterpret_cast<int*>(n_addr));
        if (identifier == GCPTR_IDENTIFIER_HEAD) {
            // std::clog << "Identifer found at " << (void*) n_addr << std::endl;
            void* next_addr = *(reinterpret_cast<void**>(n_addr + sizeof(int) + sizeof(MarkState)));
            if (next_addr != nullptr)
                mark(next_addr);
        }
    }
}

void GCWorker::mark_v2(GCPtrBase* gcptr) {
    if (gcptr == nullptr) return;
    if (gcptr->getInlineMarkState() == MarkState::DE_ALLOCATED) {
        std::clog << "mark_v2() try to mark a deallocated object, &gcptr=" << (void*)gcptr << std::endl;
        //throw std::exception();
        return;
    }

    ObjectInfo objectInfo = gcptr->getObjectInfo();
    if (objectInfo.object_addr == nullptr || objectInfo.region == nullptr) return;
    MarkState c_markstate = GCPhase::getCurrentMarkState();
    if (useInlineMarkstate) {
        if (gcptr->getInlineMarkState() == c_markstate) {     // 标记过了
            // std::clog << "Skipping " << gcptr << " as it already marked" << std::endl;
            return;
        }
        // 客观地说，指针自愈确实应该在标记对象前面
        gcptr->setInlineMarkState(c_markstate);
    }
    // 因为有SATB的存在，并且GC期间新对象一律标为存活，因此不用担心取出来的object_addr和object_region陈旧问题
    // 但是好像object_size不一致的问题可能还是有麻烦的
    this->mark_v2(objectInfo);
}

void GCWorker::mark_v2(const ObjectInfo& objectInfo) {
    void* object_addr = objectInfo.object_addr;
    if (object_addr == nullptr) return;
    size_t object_size = objectInfo.object_size;
    GCRegion* region = objectInfo.region;
    MarkState c_markstate = GCPhase::getCurrentMarkState();

    if (!enableMemoryAllocator) {
        std::shared_lock<std::shared_mutex> read_lock(this->object_map_mutex);
        auto it = object_map.find(object_addr);
        if (it == object_map.end()) {
            std::clog << "Object not found at " << object_addr << std::endl;
            return;
        }
        read_lock.unlock();

        if (c_markstate == it->second.markState)    // 标记过了
            return;
        it->second.markState = c_markstate;
        if (object_size != it->second.objectSize) {
            std::clog << "Object size doesn't equal!" << std::endl;
            throw std::exception();
            object_size = it->second.objectSize;
        }
    } else {
        if (region == nullptr || region->isEvacuated() || !region->inside_region(object_addr, object_size)) {
            std::cerr << "Exception occurred: Evacuated region or Out of range! " <<
                "&region=" << (void*)region << ", isEvacuated=" << (region == nullptr ? -1 : region->isEvacuated()) <<
                ", object_addr=" << object_addr << ", object_size=" << object_size << std::endl;
            throw std::exception();
            return;
        }
        if (region->marked(object_addr)) return;
        region->mark(object_addr, object_size);
    }

    constexpr int SIZEOF_GCPTR = sizeof(void*) == 8 ? 64 : 40;
    constexpr int vfptr_size = sizeof(void*);
    char* cptr = reinterpret_cast<char*>(object_addr);
    for (char* n_addr = cptr; n_addr <= cptr + object_size - SIZEOF_GCPTR; n_addr += sizeof(void*)) {
        int identifier_head = *(reinterpret_cast<int*>(n_addr + vfptr_size));
        if (identifier_head == GCPTR_IDENTIFIER_HEAD) {
            constexpr auto _max = [](int x, int y) constexpr { return x > y ? x : y; };
            constexpr int tail_offset =
                sizeof(int) + sizeof(MarkState) + sizeof(void*) + sizeof(unsigned int) + _max(sizeof(bool), 4) +
                sizeof(std::shared_ptr<GCRegion>) + sizeof(std::unique_ptr<IReadWriteLock>);
            char* tail_addr = n_addr + vfptr_size + tail_offset;
            int identifier_tail = *(reinterpret_cast<int*>(tail_addr));
            if (identifier_tail == GCPTR_IDENTIFIER_TAIL) {
                GCPtrBase* next_ptr = reinterpret_cast<GCPtrBase*>(n_addr);
#if 0
                // To convert to GCPtrBase*, or continue using void* but with size_t, this is a question
                void* next_addr = *(reinterpret_cast<void**>(n_addr + sizeof(int) + sizeof(MarkState)));
                if (next_addr != nullptr) {
                    auto markstate = static_cast<MarkState>(*(n_addr + sizeof(void*) * 2));
                    if (markstate != GCPhase::getCurrentMarkState())
                        mark(next_addr);
                }
#endif
                mark_v2(next_ptr);
            } else {
                std::clog << "Identifier head found at " << (void*)n_addr << " but not found tail" << std::endl;
            }
        }
    }
}


void GCWorker::GCThreadLoop() {
    GCUtil::sleep(0.1);
    while (true) {
        {
            std::unique_lock<std::mutex> lock(this->thread_mutex);
            condition.wait(lock, [this] { return ready_; });
            ready_ = false;
        }
        if (stop_) break;
        {
            startGC();
            beginMark();
            GCUtil::stop_the_world(GCPhase::getSTWLock(), threadPool.get(), GCParameter::suspendThreadsWhenSTW);
            auto start_time = std::chrono::high_resolution_clock::now();
            triggerSATBMark();
            selectRelocationSet();
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            std::clog << "Stop-the-world duration: " << std::dec << duration.count() << " us" << std::endl;
            GCUtil::resume_the_world(GCPhase::getSTWLock());
            beginSweep();
            endGC();
            if constexpr (GCParameter::waitingForGCFinished)
                finished_gc_condition.notify_all();
        }
    }
    std::cout << "GC thread exited." << std::endl;
}

void GCWorker::wakeUpGCThread() {
    {
        std::unique_lock<std::mutex> lock(this->thread_mutex);
        ready_ = true;
    }
    condition.notify_all();
    if constexpr (GCParameter::waitingForGCFinished) {
        std::cout << "Main thread waiting for gc finished" << std::endl;
        {
            std::unique_lock<std::mutex> lock(this->finished_gc_mutex);
            finished_gc_condition.wait(lock);
        }
        std::cout << "Main thread was notified that gc finished" << std::endl;
    }
}

void GCWorker::triggerGC() {
    if (enableConcurrentMark) {
        wakeUpGCThread();
    } else {
        using namespace std;
        cout << "Triggered GC" << endl;
        printMap();
        startGC();
        beginMark();
        GCPhase::SwitchToNextPhase();       // skip satb remark
        printMap();
        beginSweep();
        printMap();
        endGC();
        cout << "End of GC" << endl;
    }
}

std::pair<void*, std::shared_ptr<GCRegion>> GCWorker::allocate(size_t size) {
    if (!enableMemoryAllocator) return std::make_pair(nullptr, nullptr);
    return memoryAllocator->allocate(size);
}

void GCWorker::registerObject(void* object_addr, size_t object_size) {
    if (enableMemoryAllocator)   // 启用bitmap的情况下会在region内分配的时候自动在bitmap内打上标记，无需再次标记
        return;

    std::unique_lock<std::shared_mutex> write_lock(this->object_map_mutex);
    if (GCPhase::duringGC())
        object_map.emplace(object_addr, GCStatus(GCPhase::getCurrentMarkState(), object_size));
    else
        object_map.emplace(object_addr, GCStatus(MarkState::REMAPPED, object_size));
}

void GCWorker::addRoot(GCPtrBase* from) {
    int poolIdx = getPoolIdx();
    std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex[poolIdx]);
    if constexpr (GCParameter::deferRemoveRoot)
        root_map[poolIdx].insert_or_assign(from, false);
    else
        root_set[poolIdx].insert(from);
}

void GCWorker::removeRoot(GCPtrBase* from) {
    int poolIdx = getPoolIdx();
    if constexpr (GCParameter::deferRemoveRoot) {
        std::shared_lock<std::shared_mutex> read_lock(this->root_set_mutex[poolIdx]);
        auto it = root_map[poolIdx].find(from);
        if (it != root_map[poolIdx].end()) {
            it->second = true;
        } else {
            read_lock.unlock();
            for (int i = 0; i < poolCount; i++) {
                if (i == poolIdx) continue;
                std::shared_lock<std::shared_mutex> read_lock2(this->root_set_mutex[i]);
                if (root_map[i].empty()) continue;
                auto it = root_map[i].find(from);
                if (it != root_map[i].end()) {
                    it->second = true;
                    return;
                }
            }
            std::cerr << "Root not found when delete?" << std::endl;
        }
    } else {
        std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex[poolIdx]);
        if (!root_set[poolIdx].erase(from)) {
            write_lock.unlock();
            for (int i = 0; i < poolCount; i++) {
                if (i == poolIdx) continue;
                std::unique_lock<std::shared_mutex> write_lock2(this->root_set_mutex[i]);
                if (root_set[i].erase(from))
                    return;
            }
            std::cerr << "Root not found when delete?" << std::endl;
        }
    }
}

void GCWorker::addSATB(void* object_addr) {
    std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
    satb_queue.push_back(object_addr);
}

void GCWorker::addSATB(const ObjectInfo& objectInfo) {
    if constexpr (GCParameter::distinctSATB) {
        std::unique_lock<std::mutex> lock(satb_queue_mutex);
        auto result = satb_set.insert(objectInfo.object_addr);
        if (!result.second) return;
    }
    if (!enableMemoryAllocator) {
        std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
        satb_queue.push_back(objectInfo.object_addr);
    } else {
        if (objectInfo.region == nullptr || objectInfo.region->isEvacuated()) {
            std::cerr << "SATB for object with evacuated region, object_addr=" << objectInfo.object_addr << std::endl;
            throw std::exception();
        }
        int pool_idx = getPoolIdx();
        std::unique_lock<std::mutex> lock(satb_queue_pool_mutex[pool_idx]);
        satb_queue_pool[pool_idx].emplace_back(objectInfo);
    }
}

void GCWorker::registerDestructor(void* object_addr, const std::function<void(void*)>& destructor, GCRegion* region) {
    if (region == nullptr) {
        std::unique_lock<std::mutex> lock(this->destructor_map_mutex);
        this->destructor_map.emplace(object_addr, destructor);
    } else {
        region->registerDestructor(object_addr, destructor);
    }
}

void GCWorker::startGC() {
    if (GCPhase::getGCPhase() == eGCPhase::NONE) {
        GCPhase::SwitchToNextPhase();
        if (enableMemoryAllocator)
            memoryAllocator->flushRegionMapBuffer();

        if (enableConcurrentMark)
            GCUtil::sleep(0.1);       // 防止gc root尚未来得及加入root_set
    } else {
        std::cerr << "GC already started" << std::endl;
    }
}

void GCWorker::beginMark() {
    if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
        auto start_time = std::chrono::high_resolution_clock::now();
        this->root_object_snapshot.clear();
        this->root_ptr_snapshot.clear();
        for (int i = 0; i < poolCount; i++) {
            if constexpr (!GCParameter::deferRemoveRoot) {
                std::shared_lock<std::shared_mutex> read_lock(this->root_set_mutex[i]);
                if (!enableMemoryAllocator) {
                    for (auto it : root_set[i]) {
                        void* ptr = it->getVoidPtr();
                        if (ptr != nullptr)
                            this->root_ptr_snapshot.push_back(ptr);
                    }
                } else {
                    for (auto& it : root_set[i]) {
                        this->mark_root(it);
                    }
                }
            } else {
                std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex[i]);
                for (auto it = root_map[i].begin(); it != root_map[i].end();) {
                    if (it->second) {
                        it = root_map[i].erase(it);
                    } else {
                        GCPtrBase* gcptr = it->first;
                        if (enableMemoryAllocator) {
                            this->mark_root(gcptr);
                        } else {
                            void* ptr = gcptr->getVoidPtr();
                            if (ptr != nullptr)
                                this->root_ptr_snapshot.push_back(ptr);
                        }
                        ++it;
                    }
                }
            }
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::clog << "Root set lock duration: " << std::dec << duration.count() << " us" << std::endl;

        if (!enableMemoryAllocator) {
            for (void* ptr : this->root_ptr_snapshot) {
                this->mark(ptr);
            }
        } else {
            if (!enableParallelGC) {
                for (const ObjectInfo& objectInfo : this->root_object_snapshot) {
                    this->mark_v2(objectInfo);
                }
            } else {
                for (int i = 0; i < gcThreadCount; i++) {
                    threadPool->execute([this, i] {
                        size_t startIndex, endIndex;
                        getParallelIndex(i, root_object_snapshot, startIndex, endIndex);
                        for (size_t j = startIndex; j < endIndex; j++) {
                            this->mark_v2(root_object_snapshot[j]);
                        }
                    });
                }
                threadPool->waitForTaskComplete(gcThreadCount);
            }
        }
    } else {
        std::cerr << "Not in concurrent marking phase" << std::endl;
    }
}

void GCWorker::triggerSATBMark() {
    if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
        GCPhase::SwitchToNextPhase();   // remark
        if (!enableConcurrentMark) return;
        if (!enableMemoryAllocator) {
            if (enableParallelGC) {
                for (int i = 0; i < gcThreadCount; i++) {
                    threadPool->execute([this, i] {
                        size_t startIndex, endIndex;
                        getParallelIndex(i, satb_queue, startIndex, endIndex);
                        for (size_t j = startIndex; j < endIndex; j++) {
                            this->mark(satb_queue[j]);
                        }
                    });
                }
                threadPool->waitForTaskComplete(gcThreadCount);
            } else {
                for (auto object_addr : satb_queue) {
                    mark(object_addr);
                }
            }
            satb_queue.clear();
        } else {
            for (int i = 0; i < poolCount; i++) {
                if (satb_queue_pool[i].empty()) continue;
                if (enableParallelGC) {
                    for (int tid = 0; tid < gcThreadCount; tid++) {
                        threadPool->execute([this, i, tid] {
                            size_t startIndex, endIndex;
                            getParallelIndex(tid, satb_queue_pool[i], startIndex, endIndex);
                            for (size_t j = startIndex; j < endIndex; j++) {
                                this->mark_v2(satb_queue_pool[i][j]);
                            }
                        });
                    }
                    threadPool->waitForTaskComplete(gcThreadCount);
                } else {
                    for (auto& objectInfo : satb_queue_pool[i]) {
                        this->mark_v2(objectInfo);
                    }
                }
                satb_queue_pool[i].clear();
            }
        }
        if constexpr (GCParameter::distinctSATB)
            satb_set.clear();
    } else
        std::clog << "Already in remark phase or in other invalid phase" << std::endl;
}

void GCWorker::selectRelocationSet() {
    if (!enableMemoryAllocator) return;
    if (GCPhase::getGCPhase() != eGCPhase::REMARK) {
        std::clog << "Already in sweeping phase or in other invalid phase" << std::endl;
        return;
    }
    GCPhase::SwitchToNextPhase();
    if (enableRelocation)
        memoryAllocator->SelectRelocationSet();
    else
        memoryAllocator->SelectClearSet();
}

void GCWorker::beginSweep() {
    if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
        if (!enableMemoryAllocator) {
            std::shared_lock<std::shared_mutex> lock(object_map_mutex);
            for (auto it = object_map.begin(); it != object_map.end();) {
                if (GCPhase::needSweep(it->second.markState)) {
                    void* object_addr = it->first;
                    if (enableDestructorSupport)
                        callDestructor(object_addr, true);
                    free(object_addr);
                    it = object_map.erase(it);
                } else {
                    ++it;
                }
            }
        } else {
            if (enableRelocation)
                memoryAllocator->triggerRelocation(enableReclaim);
            else
                memoryAllocator->triggerClear();
        }
    } else std::clog << "Invalid phase, should in sweep phase" << std::endl;
}

std::pair<void*, std::shared_ptr<GCRegion>> GCWorker::getHealedPointer(void* ptr, size_t obj_size, GCRegion* region) const {
    GCPhase::RAIISTWLock raiiStwLock(true);
    std::pair<void*, std::shared_ptr<GCRegion>> ret = region->queryForwardingTable(ptr);
    if (ret.first == nullptr) {
        if (region->isEvacuated()) {
            // region已被标识为需要转移，但尚未完成转移
            region->relocateObject(ptr, obj_size, this->memoryAllocator.get());
            ret = region->queryForwardingTable(ptr);
            if (ret.first == nullptr)
                throw std::exception();
            return ret;
        } else {
            return std::make_pair(nullptr, nullptr);
        }
    } else {
        return ret;
    }
}

void GCWorker::callDestructor(void* object_addr, bool remove_after_call) {
    auto destructor_it = destructor_map.find(object_addr);
    if (destructor_it != destructor_map.end()) {
        auto& destructor = destructor_it->second;
        destructor(object_addr);
        if (remove_after_call)
            destructor_map.erase(destructor_it);
    }
}

void GCWorker::endGC() {
    if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
        GCPhase::SwitchToNextPhase();
        if (enableMemoryAllocator)
            memoryAllocator->resetLiveSize();
        root_object_snapshot.clear();
    } else {
        std::clog << "Not started GC, or not finished sweeping yet" << std::endl;
    }
}

void GCWorker::printMap() const {
    using namespace std;
    cout << "Object map: {" << endl;
    for (auto& it : object_map) {
        cout << "\t";
        cout << it.first << ": " << MarkStateUtil::toString(it.second.markState) <<
             ", size=" << it.second.objectSize;
        cout << ";" << endl;
    }
    cout << "}" << endl;
    cout << "Rootset (snapshot): { ";
    for (auto ptr : root_ptr_snapshot) {
        cout << ptr << " ";
    }
    cout << "}" << endl;
}

bool GCWorker::is_root(void* gcptr_addr) {
    if (enableMemoryAllocator) {
        return !memoryAllocator->inside_allocated_regions(gcptr_addr);
    } else {
        return GCUtil::is_stack_pointer(gcptr_addr);
    }
}