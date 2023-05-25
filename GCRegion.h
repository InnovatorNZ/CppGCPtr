#ifndef CPPGCPTR_GCREGION_H
#define CPPGCPTR_GCREGION_H

#include <iostream>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include "GCBitMap.h"
#include "GCPhase.h"

enum class RegionEnum {
    SMALL, MEDIUM, LARGE
};

class RegionEnumUtil {
public:
    static short toShort(RegionEnum regionEnum);

    static RegionEnum toRegionEnum(short e);
};

class GCRegion {
private:
    int id;
    RegionEnum regionType;
    void* startAddress;
    size_t total_size;
    std::atomic<size_t> c_offset;
    std::atomic<size_t> frag_size;
    GCBitMap bitmap;
    std::mutex region_mtx;

public:
    struct GCRegionHash {
        size_t operator()(const GCRegion& p) const;
    };

    GCRegion(int id, RegionEnum regionType, void* startAddress, size_t total_size);

    GCRegion(const GCRegion&) = delete;

    GCRegion(GCRegion&&);

    bool operator==(const GCRegion&) const;

    size_t getTotalSize() const { return total_size; }

    void* allocate(size_t size);

    void free(void* addr, size_t size);

    float getFragmentRatio() const;

    float getFreeRatio() const;
};


#endif //CPPGCPTR_GCREGION_H
