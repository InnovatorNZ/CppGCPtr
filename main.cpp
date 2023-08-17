#include <iostream>
#include <thread>
#include <string>
#include "GCPtr.h"

#define TRIGGER_GC 1
#define MULTITHREAD_TEST 0
#define DESTRUCTOR_TEST 0

class MyObject2 {
public:
    int a;
    float b;
    double c[1024];
};

class MyObject {
public:
    int a;
    double b;
#if DESTRUCTOR_TEST
    std::string c;
#endif
    GCPtr<MyObject> d;
    double f;
    GCPtr<MyObject> e;
    int h;
    double l[256];
    MyObject2* m;

    MyObject() : a(rand() % RAND_MAX),
#if DESTRUCTOR_TEST
        c("Hello, GCPtr!"),
#endif
        b(0), h(0), f(0) {
#if DESTRUCTOR_TEST
        m = new MyObject2();
#else
        m = nullptr;
#endif
    }

    void setG(GCPtr<MyObject> _g) {
        this->g = _g;
    }

    int addH() {
        h++;
        return h;
    }

    MyObject(MyObject&& other) noexcept :
#if DESTRUCTOR_TEST
        c(std::move(other.c)),
#endif
        d(std::move(other.d)) {
        this->a = other.a;
        this->b = other.b;
        this->f = other.f;
        this->e = std::move(other.e);
        this->h = other.h;
        ::memcpy(this->l, other.l, sizeof(other.l));
        this->m = other.m;
        other.m = nullptr;
    }

    ~MyObject() {
        delete m;
        m = nullptr;
    }

private:
    GCPtr<MyObject> g;
};

class Base {
public:
    int a;
    float b;

    virtual ~Base() = default;

    virtual void print() = 0;
};

class Derived : public Base {
public:
    double c;

    explicit Derived(double _c) : c(_c) {}

    void print() override {
        std::cout << c << std::endl;
    }
};

bool in_aobj_func(void* gcptr, GCPtr<MyObject> _aobj[], int arr_size) {
    for (int i = 0; i < arr_size; i++) {
        if (&_aobj[i] == gcptr) {
            return true;
        }
    }
    return false;
}

GCPtr<MyObject> obj3;

int main() {
    using namespace std;
    cout << "Size of MyObject: " << sizeof(MyObject) << endl;
    cout << "Size of GCPtr: " << sizeof(GCPtr<void>) << endl;
    cout << "Ready to start..." << endl;
    const int n = 25;
    long long time_ = 0;
    Sleep(500);

    for (int i = 0; i < n; i++) {
        auto start_time = chrono::steady_clock::now();
        GCPtr<MyObject> obj2;
        {
            GCPtr<MyObject> obj1 = gc::make_gc<MyObject>();
            obj2 = obj1;
        }
        obj3 = obj2;
        {
            obj3->d = gc::make_gc<MyObject>();
            obj3->d->a = 173;
            obj3->d->e = gc::make_gc<MyObject>();
            obj3->e = gc::make_gc<MyObject>();
            obj3->e->f = 12.43;
            GCPtr<MyObject> obj4 = gc::make_gc<MyObject>();
            GCPtr<MyObject> obj5 = gc::make_gc<MyObject>();
            obj4->setG(obj5);     //还是要运行时判断是不是栈变量啊
            obj2->setG(gc::make_gc<MyObject>());
        }
        GCPtr<Base> polyTestVar = gc::make_gc<Derived>(3.14);
        for (int j = 0; j <= 100; j++) {
            GCPtr<Base> polytest2 = gc::make_gc<Derived>(2.71828);
        }
#if 0
        cout << &obj1 << " " << &obj2 << " " << &obj3 << " " << &obj3->d << " " <<
            &obj3->e << " " << &obj3->d->d << endl;
        cout << obj3->e->f << endl;
#endif

#if TRIGGER_GC
        gc::triggerGC();
#endif

        obj3->e->f = 114.514;
        GCPtr<MyObject> obj5 = gc::make_gc<MyObject>();
        {
            GCPtr<MyObject> obj4 = gc::make_gc<MyObject>();
        }

#if MULTITHREAD_TEST
        const int th_num = 6;
        std::thread th[th_num];
        for (int tid = 0; tid < th_num; tid++) {
            th[tid] = std::thread([] {
                GCPtr<MyObject> thObj1 = gc::make_gc<MyObject>();
                for (int k = 0; k < 100; k++) {
                    GCPtr<MyObject> thObj2 = gc::make_gc<MyObject>();
                }
            });
        }
#endif

        double _f = obj3->e->f;
        obj3 = nullptr;
        obj2 = nullptr;
        GCPtr<MyObject> obj6 = gc::make_gc<MyObject>();
        GCPtr<MyObject> obj7 = gc::make_gc<MyObject>();
        {
            GCPtr<MyObject> obj8 = gc::make_gc<MyObject>();
        }
        // GCPtr<vector<GCPtr<MyObject>>> gcptr_vec = gc::make_gc<vector<GCPtr<MyObject>>>();  //待测试，GCPtr是否与std::标准库兼容

        GCPtr<MyObject> obj9 = gc::make_gc<MyObject>();
        {
            srand(time(0));
            const int arr_size = 256;
            GCPtr<MyObject> aobj[arr_size];
            for (int j = 0; j < 100000; j++) {
                GCPtr<MyObject> temp_obj = gc::make_gc<MyObject>();
                temp_obj->addH();
                if (rand() % 7 == 0)
                    obj9 = temp_obj;
                if (rand() % 11 == 0)
                    aobj[rand() % arr_size] = temp_obj;
                temp_obj->f = 6294.83;
                temp_obj->addH();
                int r = rand() % arr_size;
                if (aobj[r] != nullptr) {
                    aobj[r]->b = 7.17;
                    double _b = aobj[r]->b;
                }
            }
        }
        auto end_time = chrono::steady_clock::now();
        long long duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
        cout << "User thread duration: " << duration << " ms" << endl;
        time_ += duration;

#if MULTITHREAD_TEST
        for (int tid = 0; tid < th_num; tid++)
            th[tid].join();
#endif

#if TRIGGER_GC
        gc::triggerGC();
#endif
        Sleep(1500);
    }
    cout << "Average user thread duration: " << (double) time_ / (double) n << " ms" << endl;

    return 0;
}