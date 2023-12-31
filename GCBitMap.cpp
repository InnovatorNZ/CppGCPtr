#include "GCBitMap.h"

GCBitMap::GCBitMap(void* region_start_addr, size_t region_size, IMemoryAllocator* memoryAllocator,
                   bool mark_obj_size, int iterate_step_size, int region_to_bitmap_ratio) :
        region_start_addr(region_start_addr), region_to_bitmap_ratio(region_to_bitmap_ratio),
        memoryAllocator(memoryAllocator), mark_obj_size(mark_obj_size), iterate_step_size(iterate_step_size) {
    int bitmap_size_ = ceil((double)region_size / (double)region_to_bitmap_ratio * SINGLE_OBJECT_MARKBIT / 8);
    this->bitmap_size = bitmap_size_;
    void* bitmap_arr_memory;
    if (GCParameter::bitmapMemoryFromSecondary)
        bitmap_arr_memory = memoryAllocator->allocate_raw(bitmap_size_ * sizeof(std::atomic<unsigned char>));
    else
        bitmap_arr_memory = ::malloc(bitmap_size_ * sizeof(std::atomic<unsigned char>));
    this->bitmap_arr = static_cast<std::atomic<unsigned char>*>(bitmap_arr_memory);
}

GCBitMap::~GCBitMap() {
    if (GCParameter::bitmapMemoryFromSecondary)
        memoryAllocator->free(this->bitmap_arr, bitmap_size * sizeof(std::atomic<unsigned char>));
    else
        ::free(this->bitmap_arr);
    this->bitmap_arr = nullptr;
}

GCBitMap::GCBitMap(const GCBitMap& other) : region_to_bitmap_ratio(other.region_to_bitmap_ratio),
                                            mark_obj_size(other.mark_obj_size),
                                            iterate_step_size(other.iterate_step_size),
                                            memoryAllocator(other.memoryAllocator) {
    std::clog << "GCBitMap(const GCBitMap&)" << std::endl;
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = static_cast<std::atomic<unsigned char>*>(::malloc(bitmap_size * sizeof(std::atomic<unsigned char>)));
    if (bitmap_arr == nullptr) throw std::exception();
    for (int i = 0; i < bitmap_size; i++)
        this->bitmap_arr[i].store(other.bitmap_arr[i].load());
}

GCBitMap::GCBitMap(GCBitMap&& other) noexcept : region_to_bitmap_ratio(other.region_to_bitmap_ratio),
                                                mark_obj_size(other.mark_obj_size),
                                                iterate_step_size(other.iterate_step_size),
                                                memoryAllocator(other.memoryAllocator) {
    this->bitmap_size = other.bitmap_size;
    this->region_start_addr = other.region_start_addr;
    this->bitmap_arr = std::move(other.bitmap_arr);
    other.bitmap_size = 0;
    other.bitmap_arr = nullptr;
    other.region_start_addr = nullptr;
}

bool GCBitMap::mark(void* object_addr, unsigned int object_size, MarkStateBit state, bool overwrite) {
    if (bitmap_arr == nullptr) return false;
    object_size = alignUpSize(object_size);
    int offset = static_cast<int>(reinterpret_cast<char*>(object_addr) - reinterpret_cast<char*>(region_start_addr));
    int offset_end = static_cast<int>(reinterpret_cast<char*>(object_addr) + object_size - reinterpret_cast<char*>(region_start_addr)) - 1;
    if (offset < 0 || offset_end >= bitmap_size * region_to_bitmap_ratio * 8 / SINGLE_OBJECT_MARKBIT || offset % region_to_bitmap_ratio != 0) {
        std::clog << "Warning: Object address out of bitmap range, or is not divided exactly by ratio" << std::endl;
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
            if (!overwrite) {
                unsigned char c_markstate = c_value >> offset_bit & 3;
                if (c_markstate == ch_state) {
                    std::clog << "Info: Bitmap found already marked at " << object_addr << std::endl;
                    return false;
                }
            }
            unsigned char other_value = c_value & reserve_mask;
            unsigned char final_result = other_value | ch_state << offset_bit;
            // bitmap_arr[offset_byte] = final_result;
            if (bitmap_arr[offset_byte].compare_exchange_weak(c_value, final_result))
                break;
        }
        // 对象大小嵌入位图中
        if (mark_obj_size) {
            auto mark_obj_size_func = [this, offset_byte, object_size] {
                unsigned char s0 = object_size & 0xff;
                unsigned char s1 = object_size >> 8 & 0xff;
                unsigned char s2 = object_size >> 16 & 0xff;
                unsigned char s3 = object_size >> 24 & 0xff;
                bitmap_arr[offset_byte + 1] = s0;
                bitmap_arr[offset_byte + 2] = s1;
                bitmap_arr[offset_byte + 3] = s2;
                bitmap_arr[offset_byte + 4] = s3;
            };
            if (!overwrite) {
                unsigned int ori_obj_size = *reinterpret_cast<unsigned int*>(bitmap_arr + offset_byte + 1);
                if (ori_obj_size != 0 && ori_obj_size != object_size) {
                    std::string errorMsg = std::format("GCBitMap::mark(): Different object size found in bitmap. Original: {}, Target: {}",
                                                       ori_obj_size, object_size);
                    throw std::runtime_error(errorMsg);
                } else {
                    mark_obj_size_func();
                }
            } else {
                mark_obj_size_func();
            }
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
    return true;
}

MarkStateBit GCBitMap::getMarkState(void* object_addr) const {
    if (bitmap_arr == nullptr) return MarkStateBit::NOT_ALLOCATED;
    int offset_byte, offset_bit;
    addr_to_bit(object_addr, offset_byte, offset_bit);
    unsigned char value = bitmap_arr[offset_byte].load() >> offset_bit & 3;
    return MarkStateUtil::toMarkState(value);
}

unsigned int GCBitMap::getObjectSize(void* object_addr) const {
    if (!mark_obj_size || bitmap_arr == nullptr) return 0;
    int offset_byte, offset_bit;
    addr_to_bit(object_addr, offset_byte, offset_bit);
    unsigned int s0 = static_cast<unsigned int>(bitmap_arr[offset_byte + 1].load());
    unsigned int s1 = static_cast<unsigned int>(bitmap_arr[offset_byte + 2].load());
    unsigned int s2 = static_cast<unsigned int>(bitmap_arr[offset_byte + 3].load());
    unsigned int s3 = static_cast<unsigned int>(bitmap_arr[offset_byte + 4].load());
    unsigned int objSize = s0 | s1 << 8 | s2 << 16 | s3 << 24;
    return objSize;
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
    unsigned int obj_size = bitmap.getObjectSize((char*)bitmap.region_start_addr + getCurrentOffset());
    if (obj_size != objSize) {
        throw std::runtime_error(std::format("Object size verified failed in bitmap, {} vs {}", obj_size, objSize));
    }
    return objSize;
}

bool GCBitMap::BitMapIterator::MoveNext() {
    if (byte_offset == -1 || bit_offset == -1) {
        // 初始化迭代器
        byte_offset = 0;
        bit_offset = 0;
    }
    else {
        if (bitmap.mark_obj_size) {
            // 若启用在位图中标记对象大小，则按对象大小进行迭代
            void* next_addr = (char*)bitmap.region_start_addr + getCurrentOffset() + getCurrentObjectSize();
            bitmap.addr_to_bit(next_addr, this->byte_offset, this->bit_offset);
        }
        else if (bitmap.iterate_step_size > 0) {
            // 若未启用在位图中标记对象大小，但指定了迭代步长
            void* next_addr = (char*)bitmap.region_start_addr + getCurrentOffset() + bitmap.iterate_step_size;
            bitmap.addr_to_bit(next_addr, this->byte_offset, this->bit_offset);
        }
        else {
            // 若未启用在位图中标记对象大小
            // 备注：更新版本的bitmap的数组初始化时不再赋初值，因此该方法不再适用
            std::cerr << "Deprecated iteration of bitmap." << std::endl;
            if (bit_offset == 0 && byte_offset < bitmap.bitmap_size && bitmap.bitmap_arr[byte_offset] == 0) {
                // 使用for循环跳过全0的区域，以加速迭代过程
                const int SINGLE_SKIP_SIZE = 100000;
                int prev_byte_offset = byte_offset;
                do {
                    byte_offset++;
                } while (byte_offset < bitmap.bitmap_size &&
                         bitmap.bitmap_arr[byte_offset] == 0 &&
                         byte_offset - prev_byte_offset < SINGLE_SKIP_SIZE);
            }
            else {
                // 逐bit查阅
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
