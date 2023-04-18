#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include "GCPtr.h"

class GCWorker {
    template<typename U>
    friend
    class GCPtr;

private:
    static GCWorker* instance;
    std::unordered_map<void*, void*> ref_map;

    GCWorker() = default;

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