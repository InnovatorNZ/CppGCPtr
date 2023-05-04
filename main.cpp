#define ENABLE_CONCURRENT_MARK

#include <iostream>
#include "GCPtr.h"

class MyObject {
public:
    int a;
    double b;
    std::string c;
    GCPtr<MyObject> d;
    double f;
    GCPtr<MyObject> e;
    int h;

    MyObject() : a(0), b(0), h(0), f(0) {
    }

    void setG(GCPtr<MyObject> _g) {
        this->g = _g;
    }

    int addH() {
        h++;
        return h;
    }

private:
    GCPtr<MyObject> g;
};

GCPtr<MyObject> obj3;

int main() {
    #define TRIGGER_GC
    using namespace std;
    cout << "Size of MyObject: " << sizeof(MyObject) << endl;
    cout << "Ready to start..." << endl;
    const int n = 25;
    long long time_ = 0;
    Sleep(1000);
    for (int i = 0; i < n; i++) {
        auto start_time = chrono::high_resolution_clock::now();
        GCPtr<MyObject> obj2;
        {
            GCPtr<MyObject> obj1 = gc::make_root<MyObject>();
            obj2 = obj1;
        }
        obj3 = obj2;
        {
            obj3->d = gc::make_gc<MyObject>();
            obj3->d->a = 173;
            obj3->d->e = gc::make_gc<MyObject>();
            obj3->e = gc::make_gc<MyObject>();
            obj3->e->f = 12.43;
            GCPtr<MyObject> obj4 = gc::make_root<MyObject>();
            GCPtr<MyObject> obj5 = gc::make_root<MyObject>();
            //obj4->setG(obj5);     //还是要运行时判断是不是栈变量啊
            obj2->setG(gc::make_gc<MyObject>());
        }
        // cout << &obj1 << " " << &obj2 << " " << &obj3 << " " << &obj3->d << " " <<
        //     &obj3->e << " " << &obj3->d->d << endl;
        // cout << obj3->e->f << endl;
#ifdef TRIGGER_GC
        gc::triggerGC(true);
#endif
        obj3->e->f = 114.514;
        // GCPhase::EnterAllocating();     // 这纯粹是为了防止cout死锁。。
        // cout << obj3->e->f << endl;
        // GCPhase::LeaveAllocating();
        // std::shared_ptr<MyObject> ptr = std::make_shared<MyObject>();
        // ptr->get(); ptr.get()->a;

        GCPtr<MyObject> obj5 = gc::make_root<MyObject>();
        {
            GCPtr<MyObject> obj4 = gc::make_root<MyObject>();
        }

        Sleep(200);
        double _f = obj3->e->f;
        obj3 = nullptr;
        obj2 = nullptr;
        // cout << (obj3 == nullptr) << " " << (obj2 == nullptr) << endl;
        // gc::triggerGC();
        GCPtr<MyObject> obj6 = gc::make_root<MyObject>();
        GCPtr<MyObject> obj7 = gc::make_root<MyObject>();
        {
            GCPtr<MyObject> obj8 = gc::make_root<MyObject>();
        }

        GCPtr<MyObject> obj9 = gc::make_root<MyObject>();
        GCPtr<MyObject> aobj[400];
        //GCPtr<vector<GCPtr<MyObject>>> gcptr_vec = gc::make_gc<vector<GCPtr<MyObject>>>();  //待测试，GCPtr是否与std::标准库兼容
        srand(time(0));
        for (int j = 0; j < 100000; j++) {
            GCPtr<MyObject> temp_obj = gc::make_root<MyObject>();
            temp_obj->addH();
            if (rand() % 7 == 0)
                obj9 = temp_obj;
            if (rand() % 11 == 0)
                aobj[rand() % 400] = temp_obj;
            temp_obj->f = 711.53;
            temp_obj->addH();
        }
        // Sleep(100);
        // cout << "Another gc triggering" << endl;
        // cout << "Main thread sleeping" << endl;
        auto end_time = chrono::high_resolution_clock::now();
        long long duration = chrono::duration_cast<chrono::microseconds>(end_time - start_time).count();
        cout << "User thread duration: " << duration << " us" << endl;
        time_ += duration;
#ifdef TRIGGER_GC
        gc::triggerGC(true);
#endif
        Sleep(100);
    }
    cout << "Average user thread duration: " << (double)time_ / (double)n << " us" << endl;
    return 0;
}