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
#include "GCMemoryAllocator.h"
#include "GCUtil.h"
#include "PhaseEnum.h"

class GCStatus {
public:
    MarkState markState;
    size_t objectSize;

    GCStatus(MarkState _markState, size_t _objectSize);
};

struct ObjectAndSize {
    void* object_addr;
    size_t object_size;
};

class GCWorker {
private:
    static std::unique_ptr<GCWorker> instance;
    std::unordered_map<void*, GCStatus> object_map;
    std::shared_mutex object_map_mutex;
    std::unordered_set<GCPtrBase*> root_set;
    std::shared_mutex root_set_mutex;
    std::vector<GCPtrBase*> root_ptr_snapshot;
    std::vector<void*> satb_queue;
    int poolCount;
    std::vector<std::vector<ObjectAndSize>> satb_queue_pool;
    std::unique_ptr<std::mutex[]> satb_queue_pool_mutex;
    std::mutex satb_queue_mutex;
    std::unordered_map<void*, std::function<void()>> destructor_map;
    std::mutex destructor_map_mutex;
    std::mutex thread_mutex;
    std::condition_variable condition;
    std::unique_ptr<std::thread> gc_thread;
    std::unique_ptr<GCMemoryAllocator> memoryAllocator;
    bool enableConcurrentMark, useBitmap, useInlineMarkstate, enableRelocation, enableDestructorSupport;
    bool stop_, ready_;

    GCWorker();

    GCWorker(bool concurrent, bool useBitmap, bool enableDestructorSupport = true,
        bool useInlineMarkState = true, bool useInternalMemoryManager = false,
        bool enableRelocation = false);

    void mark(void*);

    void mark_v2(GCPtrBase*);

    void mark_v2(void*, size_t);

    void threadLoop();

    void callDestructor(void*, bool remove_after_call = false);

public:
    GCWorker(const GCWorker&) = delete;

    GCWorker(GCWorker&&) = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    ~GCWorker();

    static GCWorker* getWorker();

    template<class... Args>
    static void init(Args... args) {
        GCWorker* pGCWorker = new GCWorker(args...);
        GCWorker::instance = std::unique_ptr<GCWorker>(pGCWorker);
    }

    void wakeUpGCThread();

    void triggerGC();

    void* allocate(size_t size);

    void addObject(void* object_addr, size_t object_size);

    void addRoot(GCPtrBase*);

    void removeRoot(GCPtrBase*);

    void addSATB(void* object_addr);

    void addSATB(void* object_addr, size_t object_size);

    void registerDestructor(void* object_addr, const std::function<void()>&);

    void beginMark();

    void triggerSATBMark();

    void beginSweep();

    void* getHealedPointer(void*, size_t) const;

    void endGC();

    void printMap() const;

    bool destructorEnabled() const { return enableDestructorSupport; }

    bool bitmapEnabled() const { return useBitmap; }

    bool relocationEnabled() const { return enableRelocation; }
};


namespace gc {
    void triggerGC();

    void init(bool concurrent, bool useBitmap,
              bool enableRelocation = false, bool enableDestructorSupport = false, bool useInlineMarkState = false,
              bool useInternalMemoryManager = false);
}

#endif //CPPGCPTR_GCWORKER_H