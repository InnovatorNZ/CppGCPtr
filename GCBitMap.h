#ifndef CPPGCPTR_GCBITMAP_H
#define CPPGCPTR_GCBITMAP_H

#include <iostream>
#include <cmath>
#include "PhaseEnum.h"
#include "Iterator.h"

#define SINGLE_OBJECT_MARKBIT 2     // 表示每两个bit标记一个对象

class GCBitMap {
private:
    // bitmap 采用2个bit标记一个对象，00: Not Allocated, 01: Remapped/Deleted, 10: M0, 11: M1
    int region_to_bitmap_ratio;     // bitmap的每两个bit对应于region的多少字节，默认为1，即2bit->1byte
    int bitmap_size;
    void* region_start_addr;
    unsigned char* bitmap_arr;

    class BitMapIterator : public Iterator<MarkStateBit> {
    private:
        int byte_offset;
        int bit_offset;
        const GCBitMap& bitmap;
    public:
        explicit BitMapIterator(const GCBitMap&);

        MarkStateBit next() override;

        bool hasNext() override;
    };

public:
    GCBitMap(void* region_start_addr, size_t region_size, int region_to_bitmap_ratio = 1);

    ~GCBitMap();

    GCBitMap(const GCBitMap&);

    GCBitMap(GCBitMap&&) noexcept;

    void mark(void* object_addr, size_t object_size, MarkStateBit state);

    BitMapIterator getIterator();
};


#endif //CPPGCPTR_GCBITMAP_H
