#ifndef CPPGCPTR_GCREGIONALHASHMAP_H
#define CPPGCPTR_GCREGIONALHASHMAP_H

#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <optional>
#include "GCStatus.h"
#include "GCPhase.h"
#include "Iterator.h"

class GCRegionalHashMap {
private:
    std::unordered_map<void*, GCStatus> object_map;
    std::shared_mutex map_mutex_;

public:
    class RegionalHashMapIterator : public Iterator<GCStatus> {
    private:
        GCRegionalHashMap& regionalHashmap;
    public:
        explicit RegionalHashMapIterator(GCRegionalHashMap&);

        ~RegionalHashMapIterator() override;

        GCStatus current() const override;

        bool MoveNext() override;
    };

    bool mark(void* object_addr, size_t object_size, MarkState markState, bool overwrite = false);

    std::optional<MarkState> getMarkState(void* object_addr);

    RegionalHashMapIterator getIterator() const;
};


#endif //CPPGCPTR_GCREGIONALHASHMAP_H
