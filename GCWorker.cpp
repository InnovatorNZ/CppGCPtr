#define ENABLE_CONCURRENT_MARK

#include "GCWorker.h"

std::unique_ptr<GCWorker> GCWorker::instance = nullptr;

GCStatus::GCStatus(MarkState _markState, size_t _objectSize) : markState(_markState), objectSize(_objectSize) {
}

GCWorker::GCWorker() : stop_(false), ready_(false), gc_thread(nullptr) {
}

GCWorker::GCWorker(bool concurrent) : stop_(false), ready_(false) {
    if (concurrent) {
        this->gc_thread = std::make_unique<std::thread>(&GCWorker::threadLoop, this);
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
    gc_thread->join();
}

GCWorker* GCWorker::getWorker() {
    if (instance == nullptr) {
#ifdef ENABLE_CONCURRENT_MARK
        GCWorker* pGCWorker = new GCWorker(true);
#else
        GCWorker* pGCWorker = new GCWorker();
#endif
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
        if (identifier == GCPTR_IDENTIFIER) {
            // std::clog << "Identifer found at " << (void*) n_addr << std::endl;
            void* next_addr = *(reinterpret_cast<void**>(n_addr + sizeof(void*)));
            if (next_addr != nullptr)
                mark(next_addr);
        }
    }
}

void GCWorker::threadLoop() {
    Sleep(100);
    while (true) {
        {
            std::unique_lock<std::mutex> lock(this->thread_mutex);
            condition.wait(lock, [this] { return ready_; });  //TODO: 仅限调试期间注释本行
            ready_ = false;
        }
        if (stop_) break;
        //std::clog << "Triggered concurrent GC" << std::endl;
        //GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginMark();
        //GCWorker::getWorker()->printMap();
        GCUtil::stop_the_world(GCPhase::getSTWLock());
        auto start_time = std::chrono::high_resolution_clock::now();
        GCWorker::getWorker()->triggerSATBMark();
        //GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginSweep();
        //GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->endGC();
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::clog << "Stop-the-world duration: " << std::dec << duration.count() << " us" << std::endl;
        GCUtil::resume_the_world(GCPhase::getSTWLock());
        //std::clog << "End of concurrent GC" << std::endl;
    }
}

void GCWorker::wakeUpGCThread() {
    {
        std::unique_lock<std::mutex> lock(this->thread_mutex);
        ready_ = true;
    }
    condition.notify_all();
}

void GCWorker::addObject(void* object_addr, size_t object_size) {
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
    //std::clog << "Adding SATB: " << object_addr << std::endl;
    std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
    satb_queue.push_back(object_addr);
}

void GCWorker::registerDestructor(void* object_addr, const std::function<void()>& destructor) {
    std::unique_lock<std::mutex> lock(this->destructor_map_mutex);
    this->destructor_map.emplace(object_addr, destructor);
}

void GCWorker::beginMark() {
    if (GCPhase::getGCPhase() == eGCPhase::NONE) {
        GCPhase::SwitchToNextPhase();   // concurrent mark
        auto start_time = std::chrono::high_resolution_clock::now();
        this->root_ptr_snapshot.clear();
        {
            std::shared_lock<std::shared_mutex> read_lock(this->root_set_mutex);
            for (auto& it : root_set) {
                void* ptr = it->getVoidPtr();
                if (ptr != nullptr)
                    this->root_ptr_snapshot.push_back(ptr);
            }
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        std::clog << "copy root_set duration: " << std::dec << duration.count() << " us" << std::endl;

        for (void* ptr : this->root_ptr_snapshot) {
            mark(ptr);
        }
    } else {
        std::clog << "Already in concurrent marking phase or in other invalid phase" << std::endl;
    }
}

void GCWorker::triggerSATBMark() {
    if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
        GCPhase::SwitchToNextPhase();   // remark
        for (auto object_addr : satb_queue) {
            mark(object_addr);
        }
        satb_queue.clear();
    } else {
        std::clog << "Already in remark phase or in other invalid phase" << std::endl;
    }
}

void GCWorker::beginSweep() {
    if (GCPhase::getGCPhase() == eGCPhase::REMARK) {
        GCPhase::SwitchToNextPhase();
        for (auto it = object_map.begin(); it != object_map.end();) {
            if (GCPhase::needSweep(it->second.markState)) {
                auto destructor_it = destructor_map.find(it->first);
                if (destructor_it != destructor_map.end()) {
                    std::function<void()>& destructor = destructor_it->second;
                    destructor();
                    destructor_map.erase(destructor_it);
                }
                free(it->first);
                it = object_map.erase(it);
            } else {
                ++it;
            }
        }
    } else {
        std::clog << "Already in sweeping phase or in other invalid phase" << std::endl;
    }
}

void GCWorker::endGC() {
    if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
        GCPhase::SwitchToNextPhase();
    } else {
        std::clog << "Not started GC, or not finished sweeping yet" << std::endl;
    }
}

void GCWorker::printMap() {
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
        using namespace std;
        cout << "Triggered GC" << endl;
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginMark();
        GCWorker::getWorker()->triggerSATBMark();
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->beginSweep();
        GCWorker::getWorker()->printMap();
        GCWorker::getWorker()->endGC();
        cout << "End of GC" << endl;
    }

    void triggerGC(bool concurrent) {
        if (concurrent) {
            GCWorker::getWorker()->wakeUpGCThread();
        } else {
            triggerGC();
        }
    }
}