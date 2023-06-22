#include "GCBitMap.h"

GCBitMap::GCBitMap(void* region_start_addr, size_t region_size, bool mark_obj_size, int region_to_bitmap_ratio) :
        region_start_addr(region_start_addr), region_to_bitmap_ratio(region_to_bitmap_ratio),
        mark_obj_size(mark_obj_size), mark_high_bit(false) {
    int bitmap_size_ = ceil((double)region_size / (double)region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT / 8);
    this->bitmap_size = bitmap_size_;
    this->bitmap_arr = std::make_unique<std::atomic<unsigned char>[]>(bitmap_size_);
}

GCBitMap::~GCBitMap() = default;

GCBitMap::GCBitMap(const GCBitMap& other) : region_to_bitmap_ratio(other.region_to_bitmap_ratio), mark_obj_size(other.mark_obj_size) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = std::make_unique<std::atomic<unsigned char>[]>(bitmap_size);
    for (int i = 0; i < bitmap_size; i++)
        this->bitmap_arr[i].store(other.bitmap_arr[i].load());
}

GCBitMap::GCBitMap(GCBitMap&& other) noexcept: region_to_bitmap_ratio(other.region_to_bitmap_ratio), mark_obj_size(other.mark_obj_size) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = std::move(other.bitmap_arr);
    other.bitmap_size = 0;
    other.bitmap_arr = nullptr;
    other.region_start_addr = nullptr;
}

bool GCBitMap::mark(void* object_addr, unsigned int object_size, MarkStateBit state) {
    if (bitmap_arr == nullptr) return false;
    object_size = alignUpSize(object_size);
    int offset = static_cast<int>(reinterpret_cast<char*>(object_addr) - reinterpret_cast<char*>(region_start_addr));
    int offset_end = static_cast<int>(reinterpret_cast<char*>(object_addr) + object_size - reinterpret_cast<char*>(region_start_addr)) - 1;
    if (offset < 0 || offset_end >= bitmap_size * region_to_bitmap_ratio * 8 / SINGLE_OBJECT_MARKBIT || offset % region_to_bitmap_ratio != 0) {
        std::clog << "Object address out of bitmap range, or is not divided exactly by ratio" << std::endl;
        return false;
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
            unsigned char c_markstate = c_value >> offset_bit & 3;
            if (c_markstate == ch_state) {  // someone beats us
                std::clog << "Bitmap marking found someone beats us at " << object_addr << std::endl;
                return false;
            }
            unsigned char other_value = c_value & reserve_mask;
            unsigned char final_result = other_value | ch_state << offset_bit;
            // bitmap_arr[offset_byte] = final_result;
            if (bitmap_arr[offset_byte].compare_exchange_weak(c_value, final_result))
                break;
        }
        // 对象大小嵌入位图中
        if (mark_obj_size) {
            unsigned char s0 = object_size & 0xff;
            unsigned char s1 = object_size >> 8 & 0xff;
            unsigned char s2 = object_size >> 16 & 0xff;
            unsigned char s3 = object_size >> 24 & 0xff;
            bitmap_arr[offset_byte + 1] = s0;
            bitmap_arr[offset_byte + 2] = s1;
            bitmap_arr[offset_byte + 3] = s2;
            bitmap_arr[offset_byte + 4] = s3;
        }
    }
    // 高位
    if (mark_high_bit) {
        if (mark_obj_size && object_size > region_to_bitmap_ratio) {
            offset_end = offset_end / region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT;
            int offset_byte = offset_end / 8;
            int offset_bit = offset_end % 8;
            unsigned char reserve_mask = ~(3 << offset_bit);
            while (true) {
                unsigned char c_value = bitmap_arr[offset_byte].load();
                unsigned char other_value = c_value & reserve_mask;
                unsigned char final_result = other_value | ch_state << offset_bit;
                if (bitmap_arr[offset_byte].compare_exchange_weak(c_value, final_result))
                    break;
            }
        }
    }
#if USE_SINGLE_OBJECT_MAP
    else {
        std::unique_lock<std::mutex> lock(this->single_size_set_mtx);
        single_size_set.emplace(object_addr);
    }
#endif
    //std::clog << "Bitmap marked for " << object_addr << std::endl;
    return true;
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

unsigned int GCBitMap::alignUpSize(unsigned int size) const {
    if (size % region_to_bitmap_ratio != 0) {
        size = (size / region_to_bitmap_ratio + 1) * region_to_bitmap_ratio;
    }
    return size;
}

void GCBitMap::addr_to_bit(void* addr, int& offset_byte, int& offset_bit) const {
    int offset = static_cast<int>(reinterpret_cast<char*>(addr) - reinterpret_cast<char*>(region_start_addr));
    offset = offset * SINGLE_OBJECT_MARKBIT / region_to_bitmap_ratio;
    offset_byte = offset / 8;
    offset_bit = offset % 8;
}

void* GCBitMap::bit_to_addr(int offset_byte, int offset_bit) const {
    void* addr = reinterpret_cast<void*>(reinterpret_cast<char*>(region_start_addr) +
                                         (offset_byte * 8 + offset_bit) * region_to_bitmap_ratio / SINGLE_OBJECT_MARKBIT);
    return addr;
}

GCBitMap::BitMapIterator::BitMapIterator(const GCBitMap& bitmap) : bit_offset(-1), byte_offset(-1), bitmap(bitmap) {
}

GCBitMap::BitStatus GCBitMap::BitMapIterator::current() const {
    unsigned char value = bitmap.bitmap_arr[byte_offset].load() >> bit_offset & 3;
    BitStatus ret{};
    ret.markState = MarkStateUtil::toMarkState(value);
    if (bitmap.mark_obj_size)
        ret.objectSize = getCurrentObjectSize();
    else
        ret.objectSize = 0;
    return ret;
}

unsigned int GCBitMap::BitMapIterator::getCurrentObjectSize() const {
    unsigned int s0 = static_cast<unsigned int>(bitmap.bitmap_arr[byte_offset + 1].load());
    unsigned int s1 = static_cast<unsigned int>(bitmap.bitmap_arr[byte_offset + 2].load());
    unsigned int s2 = static_cast<unsigned int>(bitmap.bitmap_arr[byte_offset + 3].load());
    unsigned int s3 = static_cast<unsigned int>(bitmap.bitmap_arr[byte_offset + 4].load());
    unsigned int objSize = s0 | s1 << 8 | s2 << 16 | s3 << 24;
    return objSize;
}

bool GCBitMap::BitMapIterator::MoveNext() {
    if (byte_offset == -1 || bit_offset == -1) {
        byte_offset = 0;
        bit_offset = 0;
    } else {
        if (bitmap.mark_obj_size && (bitmap.bitmap_arr[byte_offset] >> bit_offset & 3) != 0) {
            char* next_addr = (char*) bitmap.region_start_addr + getCurrentOffset() + getCurrentObjectSize();
            bitmap.addr_to_bit(next_addr, this->byte_offset, this->bit_offset);
        } else {
            if (bit_offset == 0 && byte_offset < bitmap.bitmap_size && bitmap.bitmap_arr[byte_offset] == 0) {
                do {
                    byte_offset++;
                } while (bitmap.bitmap_arr[byte_offset] == 0);
            } else {
                bit_offset += SINGLE_OBJECT_MARKBIT;
                if (bit_offset >= 8) {
                    byte_offset++;
                    bit_offset = 0;
                }
            }
        }
    }
    if (byte_offset >= bitmap.bitmap_size) return false;
    else return true;
}

int GCBitMap::BitMapIterator::getCurrentOffset() const {
    return (byte_offset * 8 + bit_offset) * bitmap.region_to_bitmap_ratio / SINGLE_OBJECT_MARKBIT;
}
