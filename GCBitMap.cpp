#include "GCBitMap.h"

GCBitMap::GCBitMap(void* region_start_addr, size_t region_size, int region_to_bitmap_ratio) :
        region_start_addr(region_start_addr), region_to_bitmap_ratio(region_to_bitmap_ratio) {
    int bitmap_size_ = ceil((double)region_size / (double)region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT / 8);
    this->bitmap_size = bitmap_size_;
    this->bitmap_arr = std::make_unique<std::atomic<unsigned char>[]>(bitmap_size_);
}

GCBitMap::~GCBitMap() = default;

GCBitMap::GCBitMap(const GCBitMap& other) : region_to_bitmap_ratio(other.region_to_bitmap_ratio) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = std::make_unique<std::atomic<unsigned char>[]>(bitmap_size);
    for (int i = 0; i < bitmap_size; i++)
        this->bitmap_arr[i].store(other.bitmap_arr[i].load());
}

GCBitMap::GCBitMap(GCBitMap&& other) noexcept: region_to_bitmap_ratio(other.region_to_bitmap_ratio) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = std::move(other.bitmap_arr);
    other.bitmap_size = 0;
    other.bitmap_arr = nullptr;
    other.region_start_addr = nullptr;
}

void GCBitMap::mark(void* object_addr, size_t object_size, MarkStateBit state) {
    if (bitmap_arr == nullptr) return;
    object_size = alignUpSize(object_size);
    int offset = static_cast<int>(reinterpret_cast<char*>(object_addr) - reinterpret_cast<char*>(region_start_addr));
    int offset_end = static_cast<int>(reinterpret_cast<char*>(object_addr) + object_size - reinterpret_cast<char*>(region_start_addr)) - 1;
    if (offset < 0 || offset_end >= bitmap_size * region_to_bitmap_ratio * 8 / SINGLE_OBJECT_MARKBIT || offset % region_to_bitmap_ratio != 0) {
        std::clog << "Object address out of bitmap range, or is not divided exactly by ratio" << std::endl;
        return;
    }
    unsigned char ch_state = MarkStateUtil::toChar(state);
    // 低位
    {
        offset /= region_to_bitmap_ratio;   // region的byte对应于bitmap的bit
        offset *= SINGLE_OBJECT_MARKBIT;    // 每两个bit标记一个对象
        int offset_byte = offset / 8;
        int offset_bit = offset % 8;
        unsigned char reserve_mask = ~(3 << offset_bit);
        while (true) {
            unsigned char c_value = bitmap_arr[offset_byte].load();
            unsigned char other_value = c_value & reserve_mask;
            unsigned char final_result = other_value | ch_state << offset_bit;
            // bitmap_arr[offset_byte] = final_result;
            if (bitmap_arr[offset_byte].compare_exchange_weak(c_value, final_result))
                break;
        }
    }
    // 高位
    if (object_size > region_to_bitmap_ratio) {
        offset_end = offset_end / region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT;
        int offset_end_byte = offset_end / 8;
        int offset_end_bit = offset_end % 8;
        unsigned char reserve_mask = ~(3 << offset_end_bit);
        while (true) {
            unsigned char c_value = bitmap_arr[offset_end_byte].load();
            unsigned char other_value = c_value & reserve_mask;
            unsigned char final_result = other_value | ch_state << offset_end_bit;
            if (bitmap_arr[offset_end_byte].compare_exchange_weak(c_value, final_result))
                break;
        }
    }
#if USE_SINGLE_OBJECT_MAP
    else {
        std::unique_lock<std::mutex> lock(this->single_size_set_mtx);
        single_size_set.emplace(object_addr);
    }
#endif
}

MarkStateBit GCBitMap::getMarkState(void* object_addr) const {
    if (bitmap_arr == nullptr) return MarkStateBit::NOT_ALLOCATED;
    int offset_byte, offset_bit;
    addr_to_bit(object_addr, offset_byte, offset_bit);
    unsigned char value = bitmap_arr[offset_byte].load() >> offset_bit & 3;
    return MarkStateUtil::toMarkState(value);
}

GCBitMap::BitMapIterator GCBitMap::getIterator() const {
    return GCBitMap::BitMapIterator(*this);
}

size_t GCBitMap::alignUpSize(size_t size) {
    if (size % region_to_bitmap_ratio != 0) {
        size = (size / region_to_bitmap_ratio + 1) * region_to_bitmap_ratio;
    }
    return size;
}

void GCBitMap::addr_to_bit(void* addr, int& offset_byte, int& offset_bit) const {
    int offset = static_cast<int>(reinterpret_cast<char*>(addr) - reinterpret_cast<char*>(region_start_addr));
    offset = offset / region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT;
    offset_byte = offset / 8;
    offset_bit = offset % 8;
}

void* GCBitMap::bit_to_addr(int offset_byte, int offset_bit) const {
    void* addr = reinterpret_cast<void*>(reinterpret_cast<char*>(region_start_addr) +
                                         (offset_byte * 8 + offset_bit) * region_to_bitmap_ratio / SINGLE_OBJECT_MARKBIT);
    return addr;
}

GCBitMap::BitMapIterator::BitMapIterator(const GCBitMap& bitmap) : bit_offset(0), byte_offset(0), bitmap(bitmap) {
}

MarkStateBit GCBitMap::BitMapIterator::next() {
    unsigned char value = bitmap.bitmap_arr[byte_offset] >> bit_offset & 3;
    MarkStateBit markState = MarkStateUtil::toMarkState(value);
#if USE_SINGLE_OBJECT_MAP
    BitStatus bitStatus{markState, false};
    if (!bitmap.single_size_set.empty()) {
        void* addr = reinterpret_cast<void*>(reinterpret_cast<char*>(bitmap.region_start_addr) +
                                             (byte_offset * 8 + bit_offset) * bitmap.region_to_bitmap_ratio / SINGLE_OBJECT_MARKBIT);
        if (bitmap.single_size_set.contains(addr)) bitStatus.isSingleObject = true;
    }
#endif
    bit_offset += SINGLE_OBJECT_MARKBIT;
    if (bit_offset >= 8) {
        byte_offset++;
        bit_offset = 0;
    }
    return markState;
}

bool GCBitMap::BitMapIterator::hasNext() {
    if (byte_offset >= bitmap.bitmap_size) return false;
    else return true;
}

int GCBitMap::BitMapIterator::getCurrentOffset() const {
    return (byte_offset * 8 + bit_offset) * bitmap.region_to_bitmap_ratio / SINGLE_OBJECT_MARKBIT;
}
