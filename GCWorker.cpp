#include "GCWorker.h"

std::unique_ptr<GCWorker> GCWorker::instance = nullptr;

GCStatus::GCStatus(MarkState _markState, size_t _objectSize) : markState(_markState), objectSize(_objectSize) {
}

GCWorker::GCWorker() : GCWorker(false, false, true, false, false, false) {
}

GCWorker::GCWorker(bool concurrent, bool useBitmap, bool enableDestructorSupport, bool useInlineMarkState,
                   bool useInternalMemoryManager, bool enableRelocation, bool enableParallel) :
        stop_(false), ready_(false), enableConcurrentMark(concurrent), enableParallelGC(enableParallel),
        enableRelocation(enableRelocation), enableDestructorSupport(enableDestructorSupport) {
    this->memoryAllocator = std::make_unique<GCMemoryAllocator>(useInternalMemoryManager);
    if (useBitmap) this->enableDestructorSupport = false;     // TODO: bitmap暂不支持销毁时调用析构函数
    if (enableRelocation) {
        this->useBitmap = true;
        this->useInlineMarkstate = true;
    } else {
        this->useBitmap = useBitmap;
        this->useInlineMarkstate = useInlineMarkState;
    }
    this->poolCount = std::thread::hardware_concurrency();
    this->satb_queue_pool.resize(poolCount);
    this->satb_queue_pool_mutex = std::make_unique<std::mutex[]>(poolCount);
    if (enableParallel) {
        gcthread_cnt = 4;
        threadPool = std::make_unique<ThreadPoolExecutor>(gcthread_cnt, gcthread_cnt, 0,
                                                          std::make_unique<ArrayBlockingQueue<std::function<void()>>>(4),
                                                          ThreadPoolExecutor::CallerRunsPolicy);
    } else {
        gcthread_cnt = 0;
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
        std::clog << "Not init! Initializing GCWorker with default argument (concurrent disabled, bitmap disabled)" << std::endl;
        GCWorker* pGCWorker = new GCWorker();
        instance = std::unique_ptr<GCWorker>(pGCWorker);
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
    ObjectInfo objectInfo = gcptr->getObjectInfo();

    if (objectInfo.object_addr == nullptr || objectInfo.region == nullptr) return;
    MarkState c_markstate = GCPhase::getCurrentMarkState();
    if (useInlineMarkstate) {
        if (gcptr->getInlineMarkState() == c_markstate)     // 标记过了
            return;
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

    if (!useBitmap) {
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
            std::clog << "Evacuated region or Out of range!" << std::endl;
            throw std::exception();
            return;
        }
        if (region->marked(object_addr)) return;
        region->mark(object_addr, object_size);
    }

    constexpr int SIZEOF_GCPTR = sizeof(void*) == 8 ? 56 : 36;
    char* cptr = reinterpret_cast<char*>(object_addr);
    for (char* n_addr = cptr; n_addr < cptr + object_size - SIZEOF_GCPTR; n_addr += sizeof(void*)) {
        int identifier_head = *(reinterpret_cast<int*>(n_addr));
        if (identifier_head == GCPTR_IDENTIFIER_HEAD) {
            auto _max = [](int x, int y)constexpr { return x > y ? x : y; };
            constexpr int tail_offset =
                    sizeof(int) + sizeof(MarkState) + sizeof(void*) + sizeof(unsigned int) + _max(sizeof(bool), 4) +
                    sizeof(std::shared_ptr<GCRegion>);
            char* tail_addr = n_addr + tail_offset;
            int identifier_tail = *(reinterpret_cast<int*>(tail_addr));
            if (identifier_tail == GCPTR_IDENTIFIER_TAIL) {
                GCPtrBase* next_ptr = reinterpret_cast<GCPtrBase*>(n_addr - sizeof(void*));
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
                std::clog << "Identifier head found at " << n_addr << " but not found tail" << std::endl;
            }
        }
    }
}


void GCWorker::GCThreadLoop() {
    Sleep(100);
    while (true) {
        {
            std::unique_lock<std::mutex> lock(this->thread_mutex);
            condition.wait(lock, [this] { return ready_; });  //TODO: 仅限调试期间注释本行
            ready_ = false;
        }
        if (stop_) break;
        GCWorker::getWorker()->beginMark();
        GCUtil::stop_the_world(GCPhase::getSTWLock());
        auto start_time = std::chrono::high_resolution_clock::now();
        GCWorker::getWorker()->triggerSATBMark();
        GCWorker::getWorker()->selectRelocationSet();
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::clog << "Stop-the-world duration: " << std::dec << duration.count() << " us" << std::endl;
        GCUtil::resume_the_world(GCPhase::getSTWLock());
        GCWorker::getWorker()->beginSweep();
        GCWorker::getWorker()->endGC();
    }
    std::cout << "GC thread exited." << std::endl;
}

void GCWorker::wakeUpGCThread() {
    {
        std::unique_lock<std::mutex> lock(this->thread_mutex);
        ready_ = true;
    }
    condition.notify_all();
}

void GCWorker::triggerGC() {
    if (enableConcurrentMark) {
        GCWorker::getWorker()->wakeUpGCThread();
    } else {
        using namespace std;
        cout << "Triggered GC" << endl;
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginMark();
        GCPhase::SwitchToNextPhase();       // skip satb remark
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginSweep();
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->endGC();
        cout << "End of GC" << endl;
    }
}

std::pair<void*, std::shared_ptr<GCRegion>> GCWorker::allocate(size_t size) {
    if (!useBitmap) return std::make_pair(nullptr, nullptr);
    return memoryAllocator->allocate(size);
}

void GCWorker::registerObject(void* object_addr, size_t object_size) {
    if (useBitmap)   // 启用bitmap的情况下会在region内分配的时候自动在bitmap内打上标记，无需再次标记
        return;

    std::unique_lock<std::shared_mutex> write_lock(this->object_map_mutex);
    if (GCPhase::duringGC())
        object_map.emplace(object_addr, GCStatus(GCPhase::getCurrentMarkState(), object_size));
    else
        object_map.emplace(object_addr, GCStatus(MarkState::REMAPPED, object_size));
}

void GCWorker::addRoot(GCPtrBase* from) {
    std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex);
    root_set.insert(from);
}

void GCWorker::removeRoot(GCPtrBase* from) {
    std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex);
    root_set.erase(from);
}

void GCWorker::addSATB(void* object_addr) {
    std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
    satb_queue.push_back(object_addr);
}

void GCWorker::addSATB(const ObjectInfo& objectInfo) {
    if (!useBitmap) {
        std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
        satb_queue.push_back(objectInfo.object_addr);
    } else {
        std::thread::id tid = std::this_thread::get_id();
        int pool_idx = std::hash<std::thread::id>()(tid) % poolCount;
        std::unique_lock<std::mutex> lock(satb_queue_pool_mutex[pool_idx]);
        satb_queue_pool[pool_idx].emplace_back(objectInfo);
    }
}

void GCWorker::registerDestructor(void* object_addr, const std::function<void()>& destructor) {
    std::unique_lock<std::mutex> lock(this->destructor_map_mutex);
    this->destructor_map.emplace(object_addr, destructor);
}

void GCWorker::beginMark() {
    if (GCPhase::getGCPhase() == eGCPhase::NONE) {
        GCPhase::SwitchToNextPhase();   // concurrent mark
        auto start_time = std::chrono::high_resolution_clock::now();
        this->root_object_snapshot.clear();
        this->root_ptr_snapshot.clear();
        {
            std::shared_lock<std::shared_mutex> read_lock(this->root_set_mutex);
            if (!useBitmap && !useInlineMarkstate) {
                for (auto& it : root_set) {
                    void* ptr = it->getVoidPtr();
                    if (ptr != nullptr)
                        this->root_ptr_snapshot.push_back(ptr);
                }
            } else {
                for (auto& it : root_set) {
                    this->mark_root(it);
                }
            }
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::clog << "Root set lock duration: " << std::dec << duration.count() << " us" << std::endl;

        if (!useBitmap && !useInlineMarkstate) {
            for (void* ptr : this->root_ptr_snapshot) {
                this->mark(ptr);
            }
        } else {
            if (!enableParallelGC) {
                for (const ObjectInfo& objectInfo : this->root_object_snapshot) {
                    this->mark_v2(objectInfo);
                }
            } else {
                int snum = root_object_snapshot.size() / gcthread_cnt;
                for (int i = 0; i < gcthread_cnt; i++) {
                    threadPool->execute([this, i, snum] {
                        for (int j = i * snum; j < (i + 1) * snum; j++) {
                            this->mark_v2(root_object_snapshot[j]);
                        }
                    });
                }
                threadPool->waitForTaskComplete();
            }
        }
    } else {
        std::clog << "Already in concurrent marking phase or in other invalid phase" << std::endl;
    }
}

void GCWorker::triggerSATBMark() {
    if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
        GCPhase::SwitchToNextPhase();   // remark
        if (!useBitmap) {
            for (auto object_addr : satb_queue) {
                mark(object_addr);
            }
            satb_queue.clear();
        } else {
            // TODO: 可按i并行化
            for (int i = 0; i < poolCount; i++) {
                for (auto& object_info : satb_queue_pool[i]) {
                    mark_v2(object_info);
                }
                satb_queue_pool[i].clear();
            }
        }
    } else
        std::clog << "Already in remark phase or in other invalid phase" << std::endl;
}

void GCWorker::selectRelocationSet() {
    if (!useBitmap || !enableRelocation) return;
    if (GCPhase::getGCPhase() != eGCPhase::REMARK) {
        std::clog << "Already in sweeping phase or in other invalid phase" << std::endl;
        return;
    }
    GCPhase::SwitchToNextPhase();
    memoryAllocator->SelectRelocationSet();
}

void GCWorker::beginSweep() {
    if (GCPhase::getGCPhase() == eGCPhase::REMARK)
        GCPhase::SwitchToNextPhase();
    if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
        if (!useBitmap) {
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
                memoryAllocator->triggerRelocation();
            else
                memoryAllocator->triggerClear();
        }
    } else std::clog << "Invalid phase, should in sweep phase" << std::endl;
}

std::pair<void*, std::shared_ptr<GCRegion>> GCWorker::getHealedPointer(void* ptr, size_t obj_size, GCRegion* region) const {
    std::pair<void*, std::shared_ptr<GCRegion>> ret = region->queryForwardingTable(ptr);
    if (ret.first == nullptr) {
        if (region->isEvacuated()) {
            // region已被标识为需要转移，但尚未完成转移
            region->relocateObject(ptr, obj_size, this->memoryAllocator.get());
            ret = region->queryForwardingTable(ptr);
            if (ret.first == nullptr) throw std::exception();
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
        std::function<void()>& destructor = destructor_it->second;
        destructor();
        if (remove_after_call)
            destructor_map.erase(destructor_it);
    }
}

void GCWorker::endGC() {
    if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
        GCPhase::SwitchToNextPhase();
        if (enableRelocation)
            memoryAllocator->resetLiveSize();
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


namespace gc {
    void triggerGC() {
        GCWorker::getWorker()->triggerGC();
    }

    void init(bool concurrent, bool useBitmap, bool enableRelocation, bool enableDestructorSupport, bool useInlineMarkState,
              bool useInternalMemoryManager) {
        GCWorker::init(concurrent, useBitmap, enableDestructorSupport, useInlineMarkState, useInternalMemoryManager, enableRelocation);
    }
}