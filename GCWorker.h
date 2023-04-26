#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include "GCPtrBase.h"
#include "PhaseEnum.h"

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

    GCWorker() = default;

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
            // 现已改为使用类似bitmap的方式实现mark
            int identifier = *(reinterpret_cast<int*>(n_addr));
            if (identifier == GCPTR_IDENTIFIER) {
                // std::clog << "Identifer found at " << (void*) n_addr << std::endl;
                void* next_addr = *(reinterpret_cast<void**>(n_addr + sizeof(void*)));
                if (next_addr != nullptr)
                    mark(next_addr);
            }
        }
    }

public:
    GCWorker(const GCWorker&) = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    static GCWorker* getWorker() {
        if (instance == nullptr) {
            instance = new GCWorker();
        }
        return instance;
    }

    void addObject(void* object_addr, size_t object_size) {
        std::unique_lock<std::shared_mutex> write_lock(this->object_map_mutex);
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

    void beginMark() {
        if (GCPhase::getGCPhase() == eGCPhase::NONE) {
            GCPhase::switchToNextPhase();
            for (auto it: root_set) {
                if (it->getVoidPtr() != nullptr)
                    mark(it->getVoidPtr());
            }
        } else {
            std::clog << "Already in marking phase or in other invalid phase" << std::endl;
        }
    }

    void beginSweep() {
        if (GCPhase::inMarkingPhase()) {
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


#endif //CPPGCPTR_GCWORKER_H