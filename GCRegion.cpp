#include "GCRegion.h"

GCRegion::GCRegion() : frag_size(0), c_offset(0) {
}

GCRegion::GCRegion(int id, short kind, void* startAddress, size_t total_size) :
        id(id), kind(kind), startAddress(startAddress), total_size(total_size), frag_size(0), c_offset(0) {
}

void* GCRegion::allocate(size_t size) {
    if (c_offset + size > total_size) return nullptr;
    size_t p_offset = c_offset;
    c_offset += size;
    return reinterpret_cast<void*>(reinterpret_cast<char*>(startAddress) + p_offset);
}

void GCRegion::free(void* addr, size_t size) {
    if (reinterpret_cast<char*>(addr) < reinterpret_cast<char*>(startAddress) + c_offset) {
        frag_size += size;
    }
}

float GCRegion::getFragmentRatio() {
    if (c_offset == 0) return 0;
    return (float) ((double) frag_size / (double) c_offset);
}

float GCRegion::getFreeRatio() {
    if (total_size == 0) return 0;
    return (float) (1.0 - (double) c_offset / (double) total_size);
}

bool GCRegion::operator==(const GCRegion& other) {
    return this->startAddress == other.startAddress && this->kind == other.kind && this->total_size == other.total_size;
}

size_t GCRegion::GCRegionHash::operator()(const GCRegion& p) const {
    return std::hash<void*>()(p.startAddress) ^ std::hash<size_t>()(p.total_size);
}

short RegionEnumUtil::toShort(RegionEnum regionEnum) {
    switch (regionEnum) {
        case RegionEnum::SMALL:
            return 1;
        case RegionEnum::MEDIUM:
            return 2;
        case RegionEnum::LARGE:
            return 3;
        default:
            return 0;
    }
}

RegionEnum RegionEnumUtil::toRegionEnum(short e) {
    switch (e) {
        case 1:
            return RegionEnum::SMALL;
        case 2:
            return RegionEnum::MEDIUM;
        case 3:
            return RegionEnum::LARGE;
        default:
            std::cerr << "Invalid region enum!" << std::endl;
            throw std::exception();
    }
}