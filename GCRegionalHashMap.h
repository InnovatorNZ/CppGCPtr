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
        decltype(object_map)::iterator it;
        bool iterate_begun;

    public:
        explicit RegionalHashMapIterator(GCRegionalHashMap&);

        ~RegionalHashMapIterator() override;

        GCStatus current() const override;

        bool MoveNext() override;

        void* getCurrentAddress() const;

        void setCurrentMarkState(MarkState);
    };

    GCRegionalHashMap();

    bool mark(void* object_addr, size_t object_size, MarkState markState,
              bool overwrite = false, bool emplace_if_not_exist = true);

    std::optional<MarkState> getMarkState(void* object_addr);

    RegionalHashMapIterator getIterator();

    void clear();
};


#endif //CPPGCPTR_GCREGIONALHASHMAP_H
