#ifndef CPPGCPTR_GCBITMAP_H
#define CPPGCPTR_GCBITMAP_H

#include <iostream>
#include <cmath>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <memory>
#include <stdexcept>
#include <format>
#include "GCParameter.h"
#include "PhaseEnum.h"
#include "Iterator.h"
#include "IMemoryAllocator.h"

constexpr int SINGLE_OBJECT_MARKBIT = 2;     // 表示每两个bit标记一个对象
#define USE_SINGLE_OBJECT_MAP 0

class IMemoryAllocator;

class GCBitMap {
private:
    // bitmap 采用2个bit标记一个对象，00: Not Allocated, 01: Remapped/Deleted, 10: M0, 11: M1
    int region_to_bitmap_ratio;             // bitmap的每两个bit对应于region的多少字节，默认为1，即2bit->1byte
    int bitmap_size;
    bool mark_obj_size;                     // 是否在位图中标记对象大小
    const bool mark_high_bit = false;       // 是否在位图中标记高位（已禁用）
    int iterate_step_size;                  // 若未启用在位图中标记对象大小，则指定迭代步长
    void* region_start_addr;
    IMemoryAllocator* memoryAllocator;
    std::atomic<unsigned char>* bitmap_arr;

#if USE_SINGLE_OBJECT_MAP
    std::unordered_set<void*> single_size_set;      // 存放size<=1byte的对象，由于其无法在bitmap占用头尾标识
    std::mutex single_size_set_mtx;
#endif

    void addr_to_bit(void* addr, int& offset_byte, int& offset_bit) const;

    void* bit_to_addr(int offset_byte, int offset_bit) const;

public:
    struct BitStatus {
        MarkStateBit markState;
        unsigned int objectSize;
    };

    class BitMapIterator : public Iterator<BitStatus> {
    private:
        int byte_offset;
        int bit_offset;
        const GCBitMap& bitmap;

        unsigned int getCurrentObjectSize() const;
    public:
        explicit BitMapIterator(const GCBitMap&);

        BitStatus current() const override;

        bool MoveNext() override;

        int getCurrentOffset() const;
    };

    GCBitMap(void* region_start_addr, size_t region_size, IMemoryAllocator* memoryAllocator,
             bool mark_obj_size = true, int iterate_step_size = 0, int region_to_bitmap_ratio = 1);

    ~GCBitMap();

    GCBitMap(const GCBitMap&);

    GCBitMap(GCBitMap&&) noexcept;

    bool mark(void* object_addr, unsigned int object_size, MarkStateBit state, bool overwrite = false);

    MarkStateBit getMarkState(void* object_addr) const;

    unsigned int getObjectSize(void* object_addr) const;

    BitMapIterator getIterator() const;

    unsigned int alignUpSize(unsigned int) const;
};


#endif //CPPGCPTR_GCBITMAP_H
