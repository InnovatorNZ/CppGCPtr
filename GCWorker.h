#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include "GCPtr.h"

template<typename U>
class GCPtr;

class GCWorker {
    template<typename U>
    friend
    class GCPtr;

private:
    static GCWorker* instance;
    std::unordered_map<void*, void*> ref_map;
    std::unordered_map<void*, size_t> size_map;

    GCWorker() = default;

    template<typename U>
    void mark(void* ptr_addr) {
        auto* obj = dynamic_cast<GCPtr<U>*>(reinterpret_cast<char*>(ptr_addr));
        if (obj != nullptr) {
            obj->marked = true;
            auto it = ref_map.find(ptr_addr);
            if (it != ref_map.end()) {
                mark<U>(it->second);
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

    void beginMark() {
        // TODO: begin mark
    }

    void printMap() {
        for (auto& it: ref_map) {
            std::cout << it.first << " -> " << it.second << std::endl;
        }
    }
};

GCWorker* GCWorker::instance = nullptr;


#endif //CPPGCPTR_GCWORKER_H