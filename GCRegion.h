#ifndef CPPGCPTR_GCREGION_H
#define CPPGCPTR_GCREGION_H

#include <iostream>
#include <cstdlib>
#include <vector>
#include <atomic>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cstring>
#include <stdexcept>
#include "GCBitMap.h"
#include "GCRegionalHashMap.h"
#include "GCPhase.h"
#include "GCStatus.h"
#include "GCParameter.h"
#include "PhaseEnum.h"
#include "IAllocatable.h"
#include "IMemoryAllocator.h"

class IMemoryAllocator;

enum class RegionEnum {
    SMALL, MEDIUM, LARGE, TINY
};

class RegionEnumUtil {
public:
    static short toShort(RegionEnum regionEnum);

    static RegionEnum toRegionEnum(short e);
};

class GCRegion : public IAllocatable {
    friend class MemoryAllocatorTest;

public:
    static const size_t TINY_OBJECT_THRESHOLD;
    static const size_t TINY_REGION_SIZE;
    static const size_t SMALL_OBJECT_THRESHOLD;
    static const size_t SMALL_REGION_SIZE;
    static const size_t MEDIUM_OBJECT_THRESHOLD;
    static const size_t MEDIUM_REGION_SIZE;
    static constexpr bool use_regional_hashmap = GCParameter::useRegionalHashmap;
    static constexpr bool enable_destructor = GCParameter::enableDestructorSupport;
    static constexpr bool enable_move_constructor = GCParameter::enableMoveConstructor;
private:
    void* startAddress;
    size_t total_size;
    std::atomic<size_t> allocated_offset;
    // std::atomic<size_t> frag_size;
    std::atomic<size_t> live_size;
    RegionEnum regionType;
    MarkStateBit largeRegionMarkState;      // only used in large region
    std::unique_ptr<GCBitMap> bitmap;                       // bitmap
    std::unique_ptr<GCRegionalHashMap> regionalHashMap;     // regional hash map
    std::unordered_map<void*, std::pair<void*, std::shared_ptr<GCRegion>>> forwarding_table;
    std::shared_mutex forwarding_table_mutex;
    std::unique_ptr<std::unordered_map<void*, std::function<void(void*)>>> destructor_map;
    std::shared_mutex destructor_map_mtx;
    std::unique_ptr<std::unordered_map<void*, std::function<void(void*, void*)>>> move_constructor_map;
    std::shared_mutex move_constructor_map_mtx;
    std::recursive_mutex relocation_mutex;
    std::atomic<bool> evacuated;
    std::atomic<int> use_count;             // 用于PtrGuard计数用，PtrGuard存在期间禁止重定位
    std::mutex zero_count_mutex;
    std::condition_variable zero_count_condition;

protected:
    float getFragmentRatio() const;

    float getFreeRatio() const;

    void callDestructor(void*);

    void callMoveConstructor(void*, void*);

public:
    struct GCRegionHash {
        size_t operator()(const GCRegion& p) const;
    };

    GCRegion(RegionEnum regionType, void* startAddress, size_t total_size);

    GCRegion(const GCRegion&) = delete;

    GCRegion(GCRegion&&) noexcept;

    size_t getTotalSize() const { return total_size; }

    void* getStartAddr() const { return startAddress; }

    RegionEnum getRegionType() const { return regionType; }

    void* allocate(size_t size) override;

    void free(void* addr, size_t size) override;

    void mark(void* object_addr, size_t object_size);

    bool marked(void* object_addr);

    void clearUnmarked();

    bool canFree() const;

    void free();

    bool needEvacuate() const;

    bool isEvacuated() const { return evacuated.load(); }

    void setEvacuated() { evacuated.store(true); }

    bool isFreed() const { return startAddress == nullptr && evacuated; }

    void resetLiveSize() { live_size = 0; }

    void triggerRelocation(IMemoryAllocator*);

    void relocateObject(void*, size_t, IMemoryAllocator*);

    std::pair<void*, std::shared_ptr<GCRegion>> queryForwardingTable(void*);

    bool inside_region(void*, size_t = 0) const;

    void reclaim();

    void registerDestructor(void*, const std::function<void(void*)>&);

    void registerMoveConstructor(void*, const std::function<void(void*, void*)>&);

    void inc_use_count();

    void dec_use_count();

    bool zero_use_count() const { return use_count == 0; }
};


#endif //CPPGCPTR_GCREGION_H
