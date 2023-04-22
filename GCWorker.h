#ifndef CPPGCPTR_GCWORKER_H
#define CPPGCPTR_GCWORKER_H

#include <unordered_map>
#include <map>
#include <unordered_set>
#include "GCPtrBase.h"
#include "PhaseEnum.h"

class GCWorker {
private:
    static GCWorker* instance;
    // std::unordered_map<GCPtrBase*, GCPtrBase*> ref_map;
    // std::map<GCPtrBase*, GCPtrBase*> ref_ordered_map;
    std::unordered_map<void*, size_t> size_map;
    std::unordered_set<GCPtrBase*> root_set;
    MarkState markState;

    GCWorker() : markState(MarkState::REMAPPED) {}

    void mark(void* object_addr) {
        /*
        auto it = ref_ordered_map.find(ptr_addr);
        if (ptr_addr->marked()) return;
        ptr_addr->mark();
        if (it != ref_ordered_map.end()) {
            GCPtrBase* key = it->first;
            if (key == ptr_addr) {  // 是GCPtr
                GCPtrBase* next_addr = it->second;
                mark(next_addr);
            } else {    //是包含了GCPtr的类
                std::clog << "Warning: Reference map didn't find GCPtr, traversing the object" << std::endl;
                auto size_it = size_map.find(ptr_addr);
                size_t object_size = 0;
                if (size_it != size_map.end())
                    object_size = size_it->second;
                while (it->first < ptr_addr + object_size) {
                    mark(it->second);
                    it++;
                }
            }
        }*/
        auto it = size_map.find(object_addr);
        if (it == size_map.end()) {
            std::cerr << "Warning: No object size found but marking" << std::endl;
            return;
        }
        size_t object_size = it->second;
        char* cptr = reinterpret_cast<char*>(object_addr);
        for (char* n_addr = cptr; n_addr < cptr + object_size - sizeof(void*); n_addr += sizeof(void*)) {
            // 这种方式只能是保守式的
            // 能从size_map中找到，也有可能是存放了正好等于下一个对象地址的long long，尽管概率很低但仍然存在（p=2^-64=5.4e-20)
            // 非保守式的需要更改指针地址不能更改long long
            // 但好像也不是不能搞……主要是防止用户复制GCPtr内的地址
            // 还有一个问题，在不cast到GCPtr的情况下怎么调用mark()？或者怎么cast到GCPtr？
            // 要么搞个全局hashset存放所有GCPtr，要么T*一定位于GCPtr的第8个字节处因此减去8字节偏移量即GCPtr？
            auto nextit = size_map.find(*(reinterpret_cast<void**>(n_addr)));
            if (nextit != size_map.end()) {
                mark(nextit->first);
            }
        }
    }

public:
    GCWorker(const GCWorker&) = delete;

    GCWorker& operator=(const GCWorker&) = delete;

    static GCWorker* getWorker() {
        if (instance == nullptr) {
            instance = new GCWorker();
        }
        return instance;
    }

    void addObject(void* object_addr, size_t object_size) {
        size_map.insert(std::make_pair(object_addr, object_size));
    }

    void addRoot(GCPtrBase* from) {
        root_set.insert(from);
    }

    void removeRoot(GCPtrBase* from) {
        root_set.erase(from);
    }

    void beginMark() {
        for (auto& it: root_set) {
            mark(/*???*/);
        }
    }

    void printMap() {
        for (auto& it: ref_ordered_map) {
            std::cout << it.first << " -> " << it.second << std::endl;
        }
    }
};

GCWorker* GCWorker::instance = nullptr;


#endif //CPPGCPTR_GCWORKER_H