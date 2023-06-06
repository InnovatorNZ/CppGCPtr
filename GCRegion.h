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

enum class RegionEnum {
    SMALL, MEDIUM, LARGE, TINY
};

class RegionEnumUtil {
public:
    static short toShort(RegionEnum regionEnum);

    static RegionEnum toRegionEnum(short e);
};

class GCRegion : public IAllocatable {
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
    std::unordered_map<void*, void*> forwarding_table;
    std::shared_mutex forwarding_table_mutex;
    int allFreeFlag;                        // 0: Unknown, 1: Yes, -1: No, in small, medium, tiny region
    std::atomic<bool> evacuated;
    std::vector<std::pair<void*, size_t>> live_objects;
    // bool evacuating;

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

    size_t getAllocatedSize() const { return allocated_offset; }

    void* allocate(size_t size) override;

    void free(void* addr, size_t size) override;

    void mark(void* object_addr, size_t object_size);

    bool marked(void* object_addr) const;

    float getFragmentRatio() const;

    float getFreeRatio() const;

    void FilterLive();

    bool canFree() const;

    void free();

    bool needEvacuate() const;

    bool isEvacuated() const { return evacuated; }

    void resetLiveSize() { live_size = 0; }

    void triggerRelocation(IAllocatable*);

    void relocateObject(void*, size_t, IAllocatable*);

    void* queryForwardingTable(void*);
};


#endif //CPPGCPTR_GCREGION_H
