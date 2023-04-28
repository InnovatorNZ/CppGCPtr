#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <condition_variable>
#include "GCPtrBase.h"
#include "PhaseEnum.h"
#include "GCUtil.h"

class GCStatus {
public:
    MarkState markState;
    size_t objectSize;

    GCStatus(MarkState _markState, size_t _objectSize) : markState(_markState), objectSize(_objectSize) {
    }
};

class GCWorker {
private:
    static GCWorker* instance;
    std::unordered_map<void*, GCStatus> object_map;
    std::shared_mutex object_map_mutex;
    std::unordered_set<GCPtrBase*> root_set;
    std::shared_mutex root_set_mutex;
    std::vector<void*> satb_queue;
    std::mutex satb_queue_mutex;
    std::mutex thread_mutex;
    std::condition_variable condition;
    std::thread* gc_thread;
    bool stop_;

    GCWorker() : stop_(false), gc_thread(nullptr) {
    }

    explicit GCWorker(bool concurrent) : stop_(false) {
        if (concurrent)
            this->gc_thread = new std::thread(&GCWorker::threadLoop, this);
    }

    void mark(void* object_addr) {
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

    void threadLoop() {
        while (true) {
            std::unique_lock<std::mutex> lock(this->thread_mutex);
            condition.wait(lock);
            if (stop_) break;
            GCWorker::getWorker()->printMap();
            GCWorker::getWorker()->beginMark();
            GCUtil::stop_the_world();
            GCWorker::getWorker()->triggerSATBMark();
            GCWorker::getWorker()->printMap();
            GCWorker::getWorker()->beginSweep();
            GCWorker::getWorker()->printMap();
            GCWorker::getWorker()->endGC();
            GCUtil::resume_the_world();
        }
    }

public:
    GCWorker(const GCWorker&) = delete;

    GCWorker(GCWorker&&) = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    ~GCWorker() {
        {
            std::unique_lock<std::mutex> lock(this->thread_mutex);
            stop_ = true;
        }
        condition.notify_all();
        gc_thread->join();
        delete gc_thread;
    }

    static GCWorker* getWorker() {
        if (instance == nullptr) {
#ifdef ENABLE_CONCURRENT_MARK
            instance = new GCWorker(true);
#else
            instance = new GCWorker();
#endif
        }
        return instance;
    }

    void wakeUpGCThread() {
        condition.notify_all();
    }

    void addObject(void* object_addr, size_t object_size) {
        std::unique_lock<std::shared_mutex> write_lock(this->object_map_mutex);
        if (GCPhase::duringGC())
            object_map.emplace(object_addr, GCStatus(GCPhase::getCurrentMarkState(), object_size));
        else
            object_map.emplace(object_addr, GCStatus(MarkState::REMAPPED, object_size));
    }

    void addRoot(GCPtrBase* from) {
        std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex);
        root_set.insert(from);
    }

    void removeRoot(GCPtrBase* from) {
        std::unique_lock<std::shared_mutex> write_lock(this->root_set_mutex);
        root_set.erase(from);
    }

    void addSATB(void* object_addr) {
        std::clog << "Adding SATB: " << object_addr << std::endl;
        std::unique_lock<std::mutex> lock(this->satb_queue_mutex);
        satb_queue.push_back(object_addr);
    }

    void beginMark() {
        if (GCPhase::getGCPhase() == eGCPhase::NONE) {
            GCPhase::switchToNextPhase();   // concurrent mark
            for (auto it: root_set) {
                if (it->getVoidPtr() != nullptr)
                    mark(it->getVoidPtr());
            }
        } else {
            std::clog << "Already in concurrent marking phase or in other invalid phase" << std::endl;
        }
    }

    void triggerSATBMark() {
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
            GCPhase::switchToNextPhase();   // remark
            for (auto object_addr: satb_queue) {
                mark(object_addr);
            }
            satb_queue.clear();
        } else {
            std::clog << "Already in remark phase or in other invalid phase" << std::endl;
        }
    }

    void beginSweep() {
        if (GCPhase::getGCPhase() == eGCPhase::REMARK) {
            GCPhase::switchToNextPhase();
            for (auto it = object_map.begin(); it != object_map.end();) {
                if (GCPhase::needSweep(it->second.markState)) {
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

    void endGC() {
        if (GCPhase::getGCPhase() == eGCPhase::SWEEP) {
            GCPhase::switchToNextPhase();
        } else {
            std::clog << "Not started GC, or not finished sweeping yet" << std::endl;
        }
    }

    void printMap() {
        using namespace std;
        cout << "Object map: {" << endl;
        for (auto& it: object_map) {
            cout << "\t";
            cout << it.first << ": " << MarkStateUtil::toString(it.second.markState) <<
                 ", size=" << it.second.objectSize;
            cout << ";" << endl;
        }
        cout << "}" << endl;
        cout << "Root set: { ";
        for (auto it: root_set) {
            cout << it->getVoidPtr() << " ";
        }
        cout << "}" << endl;
    }
};

GCWorker* GCWorker::instance = nullptr;

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
            // TODO: Concurrent GC
            GCWorker::getWorker()->wakeUpGCThread();
        } else {
            triggerGC();
        }
    }
}
#endif //CPPGCPTR_GCWORKER_H