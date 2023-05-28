#ifndef CPPGCPTR_GCREGION_H
#define CPPGCPTR_GCREGION_H

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include "GCMemoryAllocator.h"
#include "GCBitMap.h"
#include "GCPhase.h"
#include "PhaseEnum.h"

enum class RegionEnum {
    SMALL, MEDIUM, LARGE, TINY
};

class RegionEnumUtil {
public:
    static short toShort(RegionEnum regionEnum);

    static RegionEnum toRegionEnum(short e);
};

class GCRegion {
private:
    void* startAddress;
    size_t total_size;
    std::atomic<size_t> c_offset;
    std::atomic<size_t> frag_size;
    RegionEnum regionType;
    MarkStateBit largeRegionMarkState;      // only used in large region
    std::unique_ptr<GCBitMap> bitmap;
    std::mutex region_mtx;

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

    void* allocate(size_t size);

    void free(void* addr, size_t size);

    void mark(void* object_addr, size_t object_size);

    float getFragmentRatio() const;

    float getFreeRatio() const;

    size_t alignUpForBitmap(size_t) const;

    void clearUnmarked();

    bool canFree() const;

    bool needEvacuate() const;
};


#endif //CPPGCPTR_GCREGION_H
