#ifndef CPPGCPTR_GCREGION_H
#define CPPGCPTR_GCREGION_H

#include <iostream>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include "GCBitMap.h"
#include "GCPhase.h"
#include "PhaseEnum.h"
#include "IAllocatable.h"
#include "IMemoryAllocator.h"

class IMemoryAllocator;

enum class RegionEnum {
    SMALL, MEDIUM, LARGE, TINY
};

class RegionEnumUtil {
public:
    static short toShort(RegionEnum regionEnum);

    static RegionEnum toRegionEnum(short e);
};

class GCRegion : public IAllocatable {
    friend class MemoryAllocatorTest;
public:
    static const size_t TINY_OBJECT_THRESHOLD;
    static const size_t TINY_REGION_SIZE;
    static const size_t SMALL_OBJECT_THRESHOLD;
    static const size_t SMALL_REGION_SIZE;
    static const size_t MEDIUM_OBJECT_THRESHOLD;
    static const size_t MEDIUM_REGION_SIZE;
private:
    void* startAddress;
    size_t total_size;
    std::atomic<size_t> allocated_offset;
    // std::atomic<size_t> frag_size;
    std::atomic<size_t> live_size;
    RegionEnum regionType;
    MarkStateBit largeRegionMarkState;      // only used in large region
    std::unique_ptr<GCBitMap> bitmap;
    std::mutex region_mtx;
    std::unordered_map<void*, std::pair<void*, std::shared_ptr<GCRegion>>> forwarding_table;
    std::shared_mutex forwarding_table_mutex;
    short allFreeFlag;                        // 0: Unknown, 1: Yes, -1: No, in small, medium, tiny region
    std::atomic<bool> evacuated;

#if USE_REGINOAL_HASHMAP
    std::unordered_map<void*, GCStatus> object_map;
    std::mutex object_map_mtx;
#endif

public:
    struct GCRegionHash {
        size_t operator()(const GCRegion& p) const;
    };

    GCRegion(RegionEnum regionType, void* startAddress, size_t total_size);

    GCRegion(const GCRegion&) = delete;

    GCRegion(GCRegion&&);

    bool operator==(const GCRegion&) const;

    size_t getTotalSize() const { return total_size; }

    void* getStartAddr() const { return startAddress; }

    void* allocate(size_t size) override;

    void free(void* addr, size_t size) override;

    void mark(void* object_addr, size_t object_size);

    bool marked(void* object_addr) const;

    float getFragmentRatio() const;

    float getFreeRatio() const;

    void clearUnmarked();

    bool canFree() const;

    void free();

    bool needEvacuate() const;

    bool isEvacuated() const { return evacuated; }

    bool isFreed() const { return evacuated && startAddress == nullptr; }   // todo: 能否用作判断依据？

    void resetLiveSize() { live_size = 0; }

    void triggerRelocation(IMemoryAllocator*);

    void relocateObject(void*, size_t, IMemoryAllocator*);

    std::pair<void*, std::shared_ptr<GCRegion>> queryForwardingTable(void*);

    bool inside_region(void*, size_t = 0) const;

    // TODO: 要不要加个reclaim，重新利用已被释放的region？
};


#endif //CPPGCPTR_GCREGION_H
