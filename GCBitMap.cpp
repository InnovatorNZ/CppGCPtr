#include "GCBitMap.h"

GCBitMap::GCBitMap(void* region_start_addr, size_t region_size, int region_to_bitmap_ratio) :
        region_start_addr(region_start_addr), region_to_bitmap_ratio(region_to_bitmap_ratio) {
    int bitmap_size_ = ceil((double) region_size / (double) (region_to_bitmap_ratio * 8));
    this->bitmap_size = bitmap_size_;
    this->bitmap_arr = new unsigned char[bitmap_size_];
}

GCBitMap::~GCBitMap() {
    delete[] bitmap_arr;
    bitmap_arr = nullptr;
}

GCBitMap::GCBitMap(const GCBitMap& other) : region_to_bitmap_ratio(other.region_to_bitmap_ratio) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = new unsigned char[bitmap_size];
    for (int i = 0; i < bitmap_size; i++)
        this->bitmap_arr[i] = other.bitmap_arr[i];
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
    int offset = reinterpret_cast<char*>(object_addr) - reinterpret_cast<char*>(region_start_addr);
    if (offset < 0 || offset >= bitmap_size * region_to_bitmap_ratio || offset % region_to_bitmap_ratio != 0) {
        std::clog << "Object address out of bitmap range, or is not divided exactly by ratio" << std::endl;
        return;
    }
    offset /= region_to_bitmap_ratio;   // region的byte对应于bitmap的bit
    offset *= SINGLE_OBJECT_MARKBIT;    // 每两个bit标记一个对象
    int offset_byte = offset / 8;
    int offset_bit = offset % 8;
    unsigned char ch_state = MarkStateUtil::toChar(state);
    unsigned char reserve_mask = ~(3 << offset_bit);
    unsigned char other_value = bitmap_arr[offset_byte] & reserve_mask;
    unsigned char state_mask = ch_state << offset_bit;
    unsigned char final_result = other_value | state_mask;
    bitmap_arr[offset_byte] = final_result;
}

GCBitMap::BitMapIterator GCBitMap::getIterator() {
    return GCBitMap::BitMapIterator(*this);
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
