#ifndef CPPGCPTR_GCREGION_H
#define CPPGCPTR_GCREGION_H

#include <iostream>
#include <cstdlib>

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
    RegionEnum kind;
    void* startAddress;
    size_t total_size;
    size_t c_offset;
    size_t frag_size;

public:
    struct GCRegionHash {
        size_t operator()(const GCRegion& p) const;
    };

    GCRegion();

    GCRegion(int id, RegionEnum kind, void* startAddress, size_t total_size);

    GCRegion(const GCRegion&) = default;

    GCRegion(GCRegion&&) = default;

    bool operator==(const GCRegion&);

    size_t getTotalSize() { return total_size; }

    void* allocate(size_t size);

    void free(void* addr, size_t size);

    float getFragmentRatio();

    float getFreeRatio();
};


#endif //CPPGCPTR_GCREGION_H
