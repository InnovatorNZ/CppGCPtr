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
#include "GCParameter.h"
#include "ObjectInfo.h"
#include "GCStatus.h"
#include "PhaseEnum.h"
#include "CppExecutor/ThreadPoolExecutor.h"
#include "CppExecutor/ArrayBlockingQueue.h"

class GCWorker {
private:
    static std::unique_ptr<GCWorker> instance;
    std::unordered_map<void*, GCStatus> object_map;
    std::shared_mutex object_map_mutex;
    std::unordered_set<GCPtrBase*> root_set;
    std::shared_mutex root_set_mutex;
    std::vector<void*> root_ptr_snapshot;
    std::vector<ObjectInfo> root_object_snapshot;
    std::vector<void*> satb_queue;
    int poolCount;
    std::vector<std::vector<ObjectInfo>> satb_queue_pool;
    std::unique_ptr<std::mutex[]> satb_queue_pool_mutex;
    std::mutex satb_queue_mutex;
    std::unordered_map<void*, std::function<void(void*)>> destructor_map;
    std::mutex destructor_map_mutex;
    std::mutex thread_mutex;
    std::condition_variable condition;
    std::unique_ptr<std::thread> gc_thread;
    std::unique_ptr<GCMemoryAllocator> memoryAllocator;
    std::unique_ptr<ThreadPoolExecutor> threadPool;
    int gcThreadCount;
    bool enableConcurrentMark, enableParallelGC, enableMemoryAllocator, useInlineMarkstate,
        enableRelocation, enableDestructorSupport, enableReclaim;
    volatile bool stop_, ready_;

    void mark(void*);

    void mark_v2(GCPtrBase*);

    void mark_v2(const ObjectInfo&);

    inline void mark_root(GCPtrBase* gcptr) {
        if (gcptr == nullptr || gcptr->getVoidPtr() == nullptr) return;
        ObjectInfo objectInfo = gcptr->getObjectInfo();
        MarkState c_markstate = GCPhase::getCurrentMarkState();
        if (useInlineMarkstate) {
            if (gcptr->getInlineMarkState() == c_markstate) {
                std::clog << "Skipping " << gcptr << " as it already marked" << std::endl;
                return;
            }
            gcptr->setInlineMarkState(c_markstate);
        }
        root_object_snapshot.emplace_back(objectInfo);
    }

    void GCThreadLoop();

    void callDestructor(void*, bool remove_after_call = false);

    template<typename U>
    void getParallelIndex(int tid, const std::vector<U>& vec, size_t& startIndex, size_t& endIndex) {
        size_t snum = vec.size() / gcThreadCount;
        startIndex = tid * snum;
        if (tid == gcThreadCount - 1)
            endIndex = vec.size();
        else
            endIndex = (tid + 1) * snum;
    }

    void startGC();

    void beginMark();

    void triggerSATBMark();

    void beginSweep();

    void selectRelocationSet();

    void endGC();

public:
    GCWorker();

    GCWorker(bool concurrent, bool enableMemoryAllocator, bool enableDestructorSupport = true,
             bool useInlineMarkState = true, bool useSecondaryMemoryManager = false,
             bool enableRelocation = false, bool enableParallel = false, bool enableReclaim = false);

    GCWorker(const GCWorker&) = delete;

    GCWorker(GCWorker&&) noexcept = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    ~GCWorker();

    static GCWorker* getWorker();

    void wakeUpGCThread();

    void triggerGC();

    std::pair<void*, std::shared_ptr<GCRegion>> allocate(size_t size);

    void registerObject(void* object_addr, size_t object_size);

    void addRoot(GCPtrBase*);

    void removeRoot(GCPtrBase*);

    void addSATB(void* object_addr);

    void addSATB(const ObjectInfo&);

    void registerDestructor(void* object_addr, const std::function<void(void*)>&, GCRegion* = nullptr);

    std::pair<void*, std::shared_ptr<GCRegion>> getHealedPointer(void*, size_t, GCRegion*) const;

    void printMap() const;

    bool destructorEnabled() const { return enableDestructorSupport; }

    bool memoryAllocatorEnabled() const { return enableMemoryAllocator; }

    bool relocationEnabled() const { return enableRelocation; }

    bool is_root(void* gcptr_addr);
};

#endif //CPPGCPTR_GCWORKER_H
