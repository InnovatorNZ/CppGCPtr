#include "GCRegion.h"

GCRegion::GCRegion() : frag_size(0), c_offset(0) {
}

GCRegion::GCRegion(int id, RegionEnum regionType, void* startAddress, size_t total_size) :
        id(id), regionType(regionType), startAddress(startAddress), total_size(total_size), frag_size(0), c_offset(0) {
}

void* GCRegion::allocate(size_t size) {
    if (startAddress == nullptr) return nullptr;
    while (true) {
        size_t p_offset = c_offset;
        if (p_offset + size > total_size) return nullptr;
        if (c_offset.compare_exchange_weak(p_offset, p_offset + size))
            return reinterpret_cast<void*>(reinterpret_cast<char*>(startAddress) + p_offset);
    }
}

void GCRegion::free(void* addr, size_t size) {
    if (reinterpret_cast<char*>(addr) < reinterpret_cast<char*>(startAddress) + c_offset) {
        frag_size += size;
    }
}

float GCRegion::getFragmentRatio() const {
    if (c_offset == 0) return 0;
    return (float) ((double) frag_size / (double) c_offset);
}

float GCRegion::getFreeRatio() const {
    if (total_size == 0) return 0;
    return (float) (1.0 - (double) c_offset / (double) total_size);
}

GCRegion::GCRegion(GCRegion&& other) {
    std::unique_lock lock(other.region_mtx);
    this->id = other.id;
    this->regionType = other.regionType;
    this->startAddress = other.startAddress;
    this->total_size = other.total_size;
    this->c_offset.store(other.c_offset.load());
    this->frag_size.store(other.frag_size.load());
    other.startAddress = nullptr;
    other.c_offset = other.total_size;
}

bool GCRegion::operator==(const GCRegion& other) const {
    return this->startAddress == other.startAddress && this->regionType == other.regionType
           && this->total_size == other.total_size;
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