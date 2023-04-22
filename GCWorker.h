#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include <map>
#include <unordered_set>
#include "GCPtrBase.h"
#include "PhaseEnum.h"

class GCWorker {
private:
    static GCWorker* instance;
    std::unordered_map<GCPtrBase*, GCPtrBase*> ref_map;
    std::map<GCPtrBase*, GCPtrBase*> ref_ordered_map;
    std::unordered_map<const GCPtrBase*, size_t> size_map;
    std::unordered_set<GCPtrBase*> root_set;
    MarkState markState;

    GCWorker() : markState(MarkState::REMAPPED) {}

    void mark(GCPtrBase* ptr_addr) {
        auto it = ref_ordered_map.find(ptr_addr);
        if (ptr_addr->marked()) return;
        ptr_addr->mark();
        if (it != ref_ordered_map.end()) {
            GCPtrBase* key = it->first;
            if (key == ptr_addr) {  // 是GCPtr
                GCPtrBase* next_addr = it->second;
                mark(next_addr);
            } else {    //是包含了GCPtr的类
                std::clog << "Warning: Reference map didn't find GCPtr, traversing the object" << std::endl;
                auto size_it = size_map.find(ptr_addr);
                size_t object_size = 0;
                if (size_it != size_map.end())
                    object_size = size_it->second;
                while (it->first < ptr_addr + object_size) {
                    mark(it->second);
                    it++;
                }
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

    void insertReference(GCPtrBase* from, const GCPtrBase* to, size_t to_size) {
        ref_ordered_map.insert(std::make_pair(from, const_cast<GCPtrBase*>(to)));
        size_map.insert(std::make_pair(to, to_size));
    }

    void removeReference(GCPtrBase* from) {
        ref_ordered_map.erase(from);
        size_map.erase(from);
    }

    void addRoot(GCPtrBase* from) {
        root_set.insert(from);
    }

    void removeRoot(GCPtrBase* from) {
        root_set.erase(from);
    }

    void beginMark() {
        for (auto& it: root_set) {
            mark(it);
        }
    }

    void printMap() {
        for (auto& it: ref_ordered_map) {
            std::cout << it.first << " -> " << it.second << std::endl;
        }
    }
};

GCWorker* GCWorker::instance = nullptr;


#endif //CPPGCPTR_GCWORKER_H