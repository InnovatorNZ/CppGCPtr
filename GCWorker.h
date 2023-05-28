#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <condition_variable>

#include "GCPtrBase.h"
#include "GCUtil.h"
#include "PhaseEnum.h"
#include "GCMemoryAllocator.h"

class GCStatus {
public:
    MarkState markState;
    size_t objectSize;

    GCStatus(MarkState _markState, size_t _objectSize);
};

class GCWorker {
private:
    static std::unique_ptr<GCWorker> instance;
    std::unordered_map<void*, GCStatus> object_map;
    std::shared_mutex object_map_mutex;
    std::unordered_set<GCPtrBase*> root_set;
    std::shared_mutex root_set_mutex;
    std::vector<void*> root_ptr_snapshot;
    std::vector<void*> satb_queue;
    std::mutex satb_queue_mutex;
    std::unordered_map<void*, std::function<void()>> destructor_map;
    std::mutex destructor_map_mutex;
    std::mutex thread_mutex;
    std::condition_variable condition;
    std::unique_ptr<std::thread> gc_thread;
    std::unique_ptr<GCMemoryAllocator> memoryAllocator;
    const bool enableConcurrentMark, useBitmap, useInlineMarkstate;
    bool stop_, ready_;

    GCWorker();

    GCWorker(bool concurrent, bool useBitmap, bool useInlineMarkstate, bool useInternalMemoryManager = false);

    void mark(void*);

    void mark_v2(GCPtrBase*);

    void threadLoop();

public:
    GCWorker(const GCWorker&) = delete;

    GCWorker(GCWorker&&) = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    ~GCWorker();

    static GCWorker* getWorker();

    static void init(bool enableConcurrentMark, bool useBitmap, bool useInlineMarkstate);

    void wakeUpGCThread();

    void triggerGC();

    void addObject(void* object_addr, size_t object_size);

    void addRoot(GCPtrBase*);

    void removeRoot(GCPtrBase*);

    void addSATB(void* object_addr);

    void registerDestructor(void* object_addr, const std::function<void()>&);

    void beginMark();

    void triggerSATBMark();

    void beginSweep();

    void endGC();

    void printMap();
};


namespace gc {
    void triggerGC();

    void init(bool enableConcurrentMark, bool useBitmap, bool useInlineMarkstate);
}

#endif //CPPGCPTR_GCWORKER_H