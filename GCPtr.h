#ifndef CPPGCPTR_GCPTR_H
#define CPPGCPTR_GCPTR_H

#include <iostream>
#include <memory>
#include <unordered_map>
#include "GCPtrBase.h"
#include "GCWorker.h"

template<typename T>
class GCPtr : public GCPtrBase {
    template<typename U>
    friend class GCPtr;

private:
    T* obj;
    unsigned int obj_size;
    bool is_root;
    const int identifier_tail = GCPTR_IDENTIFIER_TAIL;

    bool needHeal() const {
        return this->obj != nullptr && GCWorker::getWorker()->relocationEnabled()
            && GCPhase::needSelfHeal(getInlineMarkState());
    }

    void selfHeal() {
        this->obj = static_cast<T*>(GCWorker::getWorker()->getHealedPointer(this->obj, this->obj_size));
        this->setInlineMarkState(MarkState::REMAPPED);
    }

public:
    GCPtr() : obj(nullptr), obj_size(0), is_root(false) {
    }

    explicit GCPtr(T* obj) : GCPtr(obj, false) {
    }

    GCPtr(T* obj, bool is_root) : is_root(is_root), obj_size(sizeof(*obj)) {
        GCPhase::EnterCriticalSection();
        this->obj = obj;
        GCWorker::getWorker()->addObject(obj, sizeof(*obj));
        if (GCWorker::getWorker()->destructorEnabled())
            GCWorker::getWorker()->registerDestructor(obj, [obj]() { obj->~T(); });
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    T* get() {
        if (this->needHeal())
            this->selfHeal();
        return this->obj;
    }

    T* get() const {
        // Calling const get() will disable pointer self-heal, which is not recommend
        if (this->needHeal())
            return GCWorker::getWorker()->getHealedPointer(this->obj);
        else
            return obj;
    }

    T* operator->() {
        return this->get();
    }

    T* operator->() const {
        return this->get();
    }

    void* getVoidPtr() override {
        return reinterpret_cast<void*>(this->get());
    }

    unsigned int getObjectSize() const override {
        return obj_size;
    }

    GCPtr<T>& operator=(const GCPtr<T>& other) {
        if (this != &other) {
            GCPhase::EnterCriticalSection();
            if (this->obj != nullptr && this->obj != other.obj && GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
            }
            this->obj = other.obj;
            this->obj_size = other.obj_size;
            this->setInlineMarkState(other.getInlineMarkState());
            // GCWorker::getWorker()->insertReference(this, &other, sizeof(*(other.get())));
            // GCWorker::getWorker()->addObject(obj, sizeof(*obj));
            this->is_root = other.is_root;
            if (is_root) {
                GCWorker::getWorker()->addRoot(this);
            }
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    GCPtr& operator=(std::nullptr_t) {
        if (this->obj != nullptr) {
            GCPhase::EnterCriticalSection();
            if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK) {
                GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
            }
            this->obj = nullptr;
            this->obj_size = 0;
            GCPhase::LeaveCriticalSection();
        }
        return *this;
    }

    bool operator==(GCPtr<T>& other) {
        return this->get() == other.get();
    }

    bool operator==(std::nullptr_t) const {
        return this->obj == nullptr;
    }

    GCPtr(const GCPtr& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Copy constructor: " << this << std::endl;
        this->obj = other.obj;
        this->obj_size = other.obj_size;
        this->is_root = other.is_root;
        this->setInlineMarkState(other.getInlineMarkState());
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    GCPtr(GCPtr&& other) {
        GCPhase::EnterCriticalSection();
        std::clog << "Move constructor: " << this << std::endl;
        this->obj = other.obj;
        this->obj_size = other.obj_size;
        this->is_root = other.is_root;
        this->setInlineMarkState(other.getInlineMarkState());
        other.obj = nullptr;
        other.obj_size = 0;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }

    template<typename U>
    GCPtr(GCPtr<U>&& other) : obj(other.obj), obj_size(other.obj_size), is_root(other.is_root) {
        this->setInlineMarkState(other.getInlineMarkState());
        other.obj = nullptr;
        if (is_root) {
            GCWorker::getWorker()->addRoot(this);
        }
    }

    ~GCPtr() override {
        GCPhase::EnterCriticalSection();
        if (GCPhase::getGCPhase() == eGCPhase::CONCURRENT_MARK && this->obj != nullptr) {
            GCWorker::getWorker()->addSATB(this->obj, this->obj_size);
        }
        if (is_root) {
            GCWorker::getWorker()->removeRoot(this);
        }
        GCPhase::LeaveCriticalSection();
    }
};

namespace gc {
    template<class T, class... Args>
    GCPtr <T> make_gc(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        if (GCWorker::getWorker()->bitmapEnabled()) {
            obj = static_cast<T*>(GCWorker::getWorker()->allocate(sizeof(T)));
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return GCPtr<T>(obj);
    }

    template<class T, class... Args>
    GCPtr <T> make_root(Args&& ... args) {
        GCPhase::EnterCriticalSection();
        T* obj = nullptr;
        if (GCWorker::getWorker()->bitmapEnabled()) {
            obj = static_cast<T*>(GCWorker::getWorker()->allocate(sizeof(T)));
            new(obj) T(std::forward<Args>(args)...);
        } else {
            obj = new T(std::forward<Args>(args)...);
        }
        GCPhase::LeaveCriticalSection();

        if (obj == nullptr) throw std::exception();
        return GCPtr<T>(obj, true);
    }

#ifdef OLD_MAKEGC
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
#endif

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
