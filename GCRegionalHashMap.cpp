#include "GCRegionalHashMap.h"

GCRegionalHashMap::GCRegionalHashMap() {
    object_map.reserve(128);
}

bool GCRegionalHashMap::mark(void* object_addr, size_t object_size, MarkState markState, bool overwrite, bool emplace_if_not_exist) {
    if (overwrite) {
        std::unique_lock<std::shared_mutex> lock(map_mutex_);
        object_map.emplace(object_addr, GCStatus{markState, object_size});
    } else {
        std::shared_lock<std::shared_mutex> r_lock(map_mutex_);
        auto it = object_map.find(object_addr);
        if (it == object_map.end()) {
            if (emplace_if_not_exist) {
                // 读锁升级为写锁，添加进object_map的条目
                // 尽管该过程非原子，但只要保证标记过了即可，live_size重复统计问题不大
                r_lock.unlock();
                std::unique_lock<std::shared_mutex> w_lock(map_mutex_);
                object_map.emplace(object_addr, GCStatus{markState, object_size});
            } else {
                return false;
            }
        } else if (it->second.markState == markState) {   // 标记过了
            return false;
        } else {
            it->second.markState = markState;
        }
    }
    return true;
}

std::optional<MarkState> GCRegionalHashMap::getMarkState(void* object_addr) {
    std::shared_lock lock(map_mutex_);
    auto it = object_map.find(object_addr);
    if (it == object_map.end()) {
        return std::nullopt;
    } else {
        return it->second.markState;
    }
}

GCRegionalHashMap::RegionalHashMapIterator GCRegionalHashMap::getIterator() {
    return RegionalHashMapIterator(*this);
}

void GCRegionalHashMap::clear() {
    std::unique_lock<std::shared_mutex> lock(map_mutex_);
    object_map.clear();
}

GCRegionalHashMap::RegionalHashMapIterator::RegionalHashMapIterator(GCRegionalHashMap& map) :
        regionalHashmap(map), iterate_begun(false) {
}

GCRegionalHashMap::RegionalHashMapIterator::~RegionalHashMapIterator() {
    if (iterate_begun)
        regionalHashmap.map_mutex_.unlock_shared();
}

GCStatus GCRegionalHashMap::RegionalHashMapIterator::current() const {
    return it->second;
}

bool GCRegionalHashMap::RegionalHashMapIterator::MoveNext() {
    if (!iterate_begun) {
        regionalHashmap.map_mutex_.lock_shared();
        iterate_begun = true;
        it = regionalHashmap.object_map.begin();
    } else {
        ++it;
    }
    if (it == regionalHashmap.object_map.end())
        return false;
    else
        return true;
}

void* GCRegionalHashMap::RegionalHashMapIterator::getCurrentAddress() const {
    if (it == regionalHashmap.object_map.end()) return nullptr;
    return it->first;
}

void GCRegionalHashMap::RegionalHashMapIterator::setCurrentMarkState(MarkState markState) {
    if (it == regionalHashmap.object_map.end()) return;
    it->second.markState = markState;
}