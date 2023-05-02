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
    bool is_root;
public:
    GCPtr() : obj(nullptr), is_root(false) {
    }

    explicit GCPtr(T* obj) : is_root(false) {
        GCPhase::EnterAllocating();
        this->obj = obj;
        GCWorker::getWorker()->addObject(obj, sizeof(*obj));
        GCPhase::LeaveAllocating();
    }

    GCPtr(T* obj, bool is_root) : is_root(is_root) {
        GCPhase::EnterAllocating();
        this->obj = obj;
        GCWorker::getWorker()->addObject(obj, sizeof(*obj));
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveAllocating();
    }

    T* get() const {
        return obj;
    }

    T* operator->() const {
        return obj;
    }

    void* getVoidPtr() const override {
        return reinterpret_cast<void*>(this->obj);
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            GCPhase::EnterAllocating();
            if (this->obj != nullptr && this->obj != other.obj && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->obj);
            }
            this->obj = other.obj;
            //GCWorker::getWorker()->insertReference(this, &other, sizeof(*(other.get())));
            //GCWorker::getWorker()->addObject(obj, sizeof(*obj));
            this->is_root = other.is_root;
            if (is_root) {
                GCWorker::getWorker()->addRoot(this);
            }
            GCPhase::LeaveAllocating();
        }
        return *this;
    }

    GCPtr& operator=(std::nullptr_t) {
        GCPhase::EnterAllocating();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->obj);
        }
        this->obj = nullptr;
        GCPhase::LeaveAllocating();
        return *this;
    }

    bool operator==(const GCPtr<T>& other) const {
        return this->obj == other.obj;
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) {
        GCPhase::EnterAllocating();
        std::clog << "Copy constructor: " << this << std::endl;
        this->obj = other.obj;
        this->is_root = other.is_root;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveAllocating();
    }

    GCPtr(GCPtr&& other) {
        GCPhase::EnterAllocating();
        std::clog << "Move constructor: " << this << std::endl;
        this->obj = other.obj;
        this->is_root = other.is_root;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveAllocating();
    }

    ~GCPtr() override {
        GCPhase::EnterAllocating();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->obj);
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
        }
        GCPhase::LeaveAllocating();
    }
};

namespace gc {
    template<typename T>
    GCPtr <T> make_gc(T* obj) {
        GCPhase::EnterAllocating();
        GCPtr<T> ret(obj);
        GCPhase::LeaveAllocating();
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root(T* obj) {
        GCPhase::EnterAllocating();
        GCPtr<T> ret(obj, true);
        GCPhase::LeaveAllocating();
        return ret;
    }

    template<typename T>
    GCPtr <T> make_gc() {
        GCPhase::EnterAllocating();
        T* obj = new T();
        GCPtr<T> ret(obj);
        GCPhase::LeaveAllocating();
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root() {
        GCPhase::EnterAllocating();
        T* obj = new T();
        GCPtr<T> ret(obj, true);
        GCPhase::LeaveAllocating();
        return ret;
    }

#ifdef MORE_USERFRIENDLY
    template<typename T>
    GCPtr<T> make_static() {
        return make_root<T>();
    }

    template<typename T>
    GCPtr<T> make_local() {
        return make_root<T>();
    }

    template<typename T>
    GCPtr<T> make_const() {
        const T* t = new T();
        return GCPtr<T>(t);
    }
#endif
}

#endif //CPPGCPTR_GCPTR_H
