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

    explicit GCPtr(T* obj) : GCPtr(obj, false) {
    }

    GCPtr(T* obj, bool is_root) : is_root(is_root) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        GCWorker::getWorker()->addObject(obj, sizeof(*obj));
        GCWorker::getWorker()->registerDestructor(obj, [obj]() { obj->~T(); });
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
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
            GCPhase::EnterCriticalSection();
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
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    GCPtr& operator=(std::nullptr_t) {
        GCPhase::EnterCriticalSection();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->obj);
        }
        this->obj = nullptr;
        GCPhase::LeaveCriticalSection();
        return *this;
    }

    bool operator==(const GCPtr<T>& other) const {
        return this->obj == other.obj;
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Copy constructor: " << this << std::endl;
        this->obj = other.obj;
        this->is_root = other.is_root;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(GCPtr&& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Move constructor: " << this << std::endl;
        this->obj = other.obj;
        this->is_root = other.is_root;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    ~GCPtr() override {
        GCPhase::EnterCriticalSection();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->obj);
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }
};

namespace gc {
    template<typename T>
    GCPtr <T> make_gc(T* obj) {
        GCPtr<T> ret(obj);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root(T* obj) {
        GCPtr<T> ret(obj, true);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_gc() {
        GCPhase::EnterCriticalSection();
        T* obj = new T();
        GCPhase::LeaveCriticalSection();
        GCPtr<T> ret(obj);
        return ret;
    }

    template<typename T>
    GCPtr <T> make_root() {
        GCPhase::EnterCriticalSection();
        T* obj = new T();
        GCPhase::LeaveCriticalSection();
        GCPtr<T> ret(obj, true);
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
