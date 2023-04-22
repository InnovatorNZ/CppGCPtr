#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include "GCPtrBase.h"
#include "GCWorker.h"

template<typename T>
class GCPtr : public GCPtrBase {
private:
    T* obj;
public:
    GCPtr() : obj(nullptr) {
    }

    explicit GCPtr(T* obj) {
        this->obj = obj;
    }

    T* get() const {
        return obj;
    }

    T* operator->() const {
        return obj;
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            // 新增this -> &other的引用链
            // std::cout << this << " -> " << &other << std::endl;
            this->obj = other.obj;
            GCWorker::getWorker()->insertReference(this, &other, sizeof(*(other.get())));
        }
        return *this;
    }

    ~GCPtr() override {
        std::clog << "~GCPtr(): " << this << std::endl;
    }
};

namespace gc {
    template<typename T>
    GCPtr<T> make_gc() {
        T* t = new T();
        // TODO: make_gc
        return GCPtr<T>(t);
    }

    template<typename T>
    GCPtr<T> make_root() {
        T* t = new T();
        // TODO: make_root_gc
        return GCPtr<T>(t);
    }

    template<typename T>
    GCPtr<T> make_static() {

    }

    template<typename T>
    GCPtr<T> make_local() {

    }

    template<typename T>
    GCPtr<T> make_const() {
        const T* t = new T();
        return GCPtr<T>(t);
    }
}

#endif //CPPGCPTR_GCPTR_H
