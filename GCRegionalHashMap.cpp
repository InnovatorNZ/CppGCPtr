#include "GCRegionalHashMap.h"

bool GCRegionalHashMap::mark(void* object_addr, size_t object_size, MarkState markState, bool overwrite) {
    if (overwrite) {
        std::unique_lock<std::shared_mutex> lock(map_mutex_);
        object_map.emplace(object_addr, GCStatus{GCPhase::getCurrentMarkState(), object_size});
    } else {

    }
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