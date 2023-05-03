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
    using namespace std;
    for (int i = 0; i < 15; i++) {
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
        // gc::triggerGC(true);
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
        //GCPtr<vector<GCPtr<MyObject>>> gcptr_vec = gc::make_gc<vector<GCPtr<MyObject>>>();  //待测试，GCPtr是否与std::标准库兼容
        for (int j = 0; j < 100000; j++) {
            GCPtr<MyObject> temp_obj = gc::make_root<MyObject>();
            temp_obj->addH();
            if (rand() % 3 == 0)
                obj9 = temp_obj;
            temp_obj->f = 711.53;
            temp_obj->addH();
        }
        // Sleep(100);
        // cout << "Another gc triggering" << endl;
        // gc::triggerGC(true);
        // cout << "Main thread sleeping" << endl;
        Sleep(500);
    }
    return 0;
}