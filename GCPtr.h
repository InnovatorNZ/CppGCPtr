#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include "GCWorker.h"

template<typename T>
class GCPtr {
    friend class GCWorker;

private:
    T* obj;
    bool marked = true;

public:
    GCPtr() : obj(nullptr) {
    }

    explicit GCPtr(T* obj) {
        this->obj = obj;
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            // 新增this -> &other的引用链
            // std::cout << this << " -> " << &other << std::endl;
            this->obj = other.obj;
            GCWorker::getWorker()->ref_map.insert(std::make_pair(
                    reinterpret_cast<void*>(this),
                    const_cast<void*>(reinterpret_cast<const void*>(&other))
            ));
        }
        return *this;
    }
};

template<typename T>
GCPtr<T> makeGC() {
    T* t = new T();
    //std::make_shared<GCPtr>();
    // TODO: make_gc
    return GCPtr<T>(t);
}

template<typename T>
GCPtr<T> makeRootGC() {
    T* t = new T();
    // TODO: make_root_gc
    return GCPtr<T>(t);
}

#endif //CPPGCPTR_GCPTR_H
