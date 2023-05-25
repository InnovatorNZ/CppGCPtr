#include "GCBitMap.h"

GCBitMap::GCBitMap(void* region_start_addr, size_t region_size, int region_to_bitmap_ratio) :
        region_start_addr(region_start_addr), region_to_bitmap_ratio(region_to_bitmap_ratio) {
    int bitmap_size_ = ceil((double) region_size / (double) (region_to_bitmap_ratio * 8));
    this->bitmap_size = bitmap_size_;
    this->bitmap_arr = new std::atomic<unsigned char>[bitmap_size_];
}

GCBitMap::~GCBitMap() {
    delete[] bitmap_arr;
    bitmap_arr = nullptr;
}

GCBitMap::GCBitMap(const GCBitMap& other) : region_to_bitmap_ratio(other.region_to_bitmap_ratio) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = new std::atomic<unsigned char>[bitmap_size];
    for (int i = 0; i < bitmap_size; i++)
        this->bitmap_arr[i].store(other.bitmap_arr[i].load());
}

GCBitMap::GCBitMap(GCBitMap&& other) noexcept: region_to_bitmap_ratio(other.region_to_bitmap_ratio) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = other.bitmap_arr;
    other.bitmap_size = 0;
    other.bitmap_arr = nullptr;
    other.region_start_addr = nullptr;
}

void GCBitMap::mark(void* object_addr, size_t object_size, MarkStateBit state) {
    // todo: object_size要align up
    int offset = static_cast<int>(reinterpret_cast<char*>(object_addr) - reinterpret_cast<char*>(region_start_addr));
    int offset_end = static_cast<int>(reinterpret_cast<char*>(object_addr) + object_size - reinterpret_cast<char*>(region_start_addr)) - 1;
    if (offset < 0 || offset_end >= bitmap_size * region_to_bitmap_ratio || offset % region_to_bitmap_ratio != 0) {
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
            unsigned char c_value = bitmap_arr[offset_byte];
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
            unsigned char c_value = bitmap_arr[offset_end_byte];
            unsigned char other_value = c_value & reserve_mask;
            unsigned char final_result = other_value | ch_state << offset_end_bit;
            if (bitmap_arr[offset_end_byte].compare_exchange_weak(c_value, final_result))
                break;
        }
    } else {
        std::unique_lock<std::mutex> lock(this->single_size_set_mtx);
        single_size_set.emplace(object_addr);
    }
}

GCBitMap::BitMapIterator GCBitMap::getIterator() const {
    return GCBitMap::BitMapIterator(*this);
}

int GCBitMap::getRegionToBitmapRatio() const {
    return region_to_bitmap_ratio;
}

GCBitMap::BitMapIterator::BitMapIterator(const GCBitMap& bitmap) : bit_offset(0), byte_offset(0), bitmap(bitmap) {
}

MarkStateBit GCBitMap::BitMapIterator::next() {
    unsigned char value = bitmap.bitmap_arr[byte_offset] >> bit_offset & 3;
    bit_offset += SINGLE_OBJECT_MARKBIT;
    if (bit_offset >= 8) {
        byte_offset++;
        bit_offset = 0;
    }
    return MarkStateUtil::toMarkState(value);
}

bool GCBitMap::BitMapIterator::hasNext() {
    if (byte_offset >= bitmap.bitmap_size) return false;
    else return true;
}
