#include <iostream>
#include <thread>
#include <string>
#include "GCPtr.h"

#define MULTITHREAD_TEST 1
#define DESTRUCTOR_TEST 0
#define WITH_STL_TEST 1

#if !_WIN32
void Sleep(int millisecond) {
    GCUtil::sleep((float) millisecond / 1000);
}
#endif

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
        memset(l, 0, sizeof(l));
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

    MyObject(MyObject&& other) noexcept:
#if DESTRUCTOR_TEST
            c(std::move(other.c)),
#endif
            d(std::move(other.d)) {
        this->a = other.a;
        this->b = other.b;
        this->f = other.f;
        this->e = std::move(other.e);
        this->h = other.h;
        memcpy(this->l, other.l, sizeof(other.l));
        this->m = other.m;
        other.m = nullptr;
    }

    ~MyObject() {
        delete m;
        m = nullptr;
        a = -1;
    }

private:
    GCPtr<MyObject> g;
};

class Base {
public:
    int a;
    float b;

    Base() : a(0), b(0.2) {}

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

template<typename T>
class MyVector {
private:
    T* dat;
    int idx = 0;
    const int maxn = 10000;
public:
    void push_back(T d) {
        dat[idx] = d;
        idx = (idx + 1) % maxn;
    }

    T at(int _idx) {
        return dat[_idx];
    }

    int size() {
        return idx == 0 ? 1 : idx;
    }

    MyVector() {
        dat = new T[maxn];
    }

    ~MyVector() {
        delete[] dat;
        dat = nullptr;
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

namespace DijkstraTest {
    using namespace std;
    const int INF = 0x7fffffff;

    struct Edge {
        int to, weight;

        Edge(int to, int weight) : to(to), weight(weight) {}
    };

    class Dijkstra {
    private:
        GCPtr<vector<vector<Edge>>> adj;

        GCPtr<vector<int>> dijkstra(int start) {
            int n = adj->size();
            GCPtr<vector<int>> dist = gc::make_gc<vector<int>>(n, INF);
            dist->at(start) = 0;
            GCPtr<priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>>> pque =
                gc::make_gc<priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>>>();
            pque->emplace(0, start);
            while (!pque->empty()) {
                int u = pque->top().second;
                pque->pop();
                for (auto& e : adj->at(u)) {
                    int v = e.to, w = e.weight;
                    if ((*dist)[u] != INF && (*dist)[u] + w < (*dist)[v]) {
                        (*dist)[v] = (*dist)[u] + w;
                        pque->emplace(dist->at(v), v);
                    }
                }
            }
            return dist;
        }

    public:
        void run(bool print_ans = true) {
            if (freopen("../in2.txt", "r", stdin) == NULL)
                if (freopen("in2.txt", "r", stdin) == NULL)
                    throw std::runtime_error("input test file not found");
            int n, m, start;
            cin >> n >> m >> start;
            adj = gc::make_gc<vector<vector<Edge>>>();
            adj->resize(n + 1);
            for (int i = 0; i < m; ++i) {
                int u, v, w;
                cin >> u >> v >> w;
                adj->at(u).emplace_back(v, w);
            }
            fclose(stdin);
            GCPtr<vector<int>> ans = dijkstra(start);
            if (print_ans) {
                for (int i = 0; i < n; ++i) {
                    cout << ans->at(i) << " ";
                }
                cout << endl;
            }
        }
    };
}

namespace LRUCacheTest {
    struct Node {
        int key, value;
        GCPtr<Node> prev, next;

        Node() : prev(nullptr), next(nullptr) {}

        Node(int key, int value) : key(key), value(value), prev(nullptr), next(nullptr) {
        }
    };

    class LinkedList {
    private:
        GCPtr<Node> head;
        GCPtr<Node> tail;
    public:
        LinkedList() {
            head = gc::make_gc<Node>(-2, 0);
            tail = gc::make_gc<Node>(-3, 0);
            head->next = tail;
            tail->prev = head;
        }

        void remove(GCPtr<Node> node) {
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }

        void insert_head(GCPtr<Node> node) {
            GCPtr<Node> next = head->next;
            node->next = next;
            node->prev = head;
            next->prev = node;
            head->next = node;
        }

        GCPtr<Node> get_tail() {
            if (tail->prev == head) return nullptr;
            return tail->prev;
        }
    };

    class LRUCache {
    private:
        const int capacity;
        GCPtr<std::unordered_map<int, GCPtr<Node>>> map;
        GCPtr<LinkedList> linkedList;

    public:
        LRUCache(int capacity) : capacity(capacity) {
            map = gc::make_gc<std::unordered_map<int, GCPtr<Node>>>();
            linkedList = gc::make_gc<LinkedList>();
        }

        int get(int key) {
            auto it = map->find(key);
            if (it == map->end()) return -1;
            GCPtr<Node> node = it->second;
            linkedList->remove(node);
            linkedList->insert_head(node);
            return node->value;
        }

        void put(int key, int value) {
            auto it = map->find(key);
            if (it != map->end()) {
                GCPtr<Node> node = it->second;
                node->value = value;
                linkedList->remove(node);
                linkedList->insert_head(node);
                return;
            }
            if (map->size() >= capacity) {
                GCPtr<Node> del = linkedList->get_tail();
                if (del == nullptr)
                    throw std::runtime_error("del is nullptr");
                linkedList->remove(del);
                map->erase(del->key);
            }
            GCPtr<Node> node = gc::make_gc<Node>(key, value);
            linkedList->insert_head(node);
            map->emplace(key, node);
        }
    };

    class LRUTest {
    private:
        const int MAXN = 997;
        GCPtr<LRUCache> lruCache;

        int get_random() {
            return rand() % MAXN / 2 + 1;
        }

        void run_single_test() {
            srand(time(0));
            int size = rand() % MAXN + 1;
            lruCache = gc::make_gc<LRUCache>(size);
            for (int i = 0; i < size * 3; i++) {
                int op = rand() % 2;
                if (op == 0) {
                    int ans = lruCache->get(get_random());
                } else if (op == 1) {
                    int v = get_random();
                    lruCache->put(v, v);
                }
            }
        }

    public:
        void run(int times = 1) {
            for (int i = 0; i < times; i++) {
                run_single_test();
            }
        }
    };
}

GCPtr<MyObject> obj3;

int main() {
    const bool triggerGC = true;
    const bool testDKThread = false;

    using namespace std;
    cout << "Size of MyObject: " << sizeof(MyObject) << endl;
    cout << "Size of GCPtr: " << sizeof(GCPtr<void>) << endl;
    cout << "Ready to start..." << endl;
    const int n = 25;
    long long time_ = 0;
    Sleep(500);

    std::thread dk_th;
    GCPtr<DijkstraTest::Dijkstra> dijkstra;
    GCPtr<LRUCacheTest::LRUTest> lruTest;
    if (testDKThread) {
        dk_th = std::thread([] {
            GCPtr<DijkstraTest::Dijkstra> dijkstra = gc::make_gc<DijkstraTest::Dijkstra>();
            GCPtr<LRUCacheTest::LRUTest> lruTest = gc::make_gc<LRUCacheTest::LRUTest>();
            for (int i = 0; i < n * 2; i++) {
                dijkstra->run(false);
                lruTest->run();
            }
        });
    } else {
        dijkstra = gc::make_gc<DijkstraTest::Dijkstra>();
        lruTest = gc::make_gc<LRUCacheTest::LRUTest>();
    }

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
        if (!testDKThread) {
            dijkstra->run(false);
            lruTest->run();
        }

        GCPtr<Base> polyTestVar = gc::make_gc<Derived>(3.14);
        for (int j = 0; j <= 100; j++) {
            GCPtr<Base> polytest2 = gc::make_gc<Derived>(2.71828);
        }

        std::this_thread::yield();
#if 0
        cout << &obj2 << " " << &obj3 << " " << &obj3->d << " " <<
            &obj3->e << " " << &obj3->d->d << " " << (obj3 == obj2) <<
            obj3->e->f << endl;
#endif

        if (triggerGC) {
            gc::triggerGC();
        }

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
                thObj1->b = 1.9191810;
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

        GCPtr<MyObject> obj9 = gc::make_gc<MyObject>();
        GCPtr<MyObject> obj10 = gc::make_gc<MyObject>();
        {
            srand(time(0));
            const int arr_size = 128;
            GCPtr<MyObject> aobj[arr_size];
            GCPtr<vector<GCPtr<MyObject>>> gcptr_vec = gc::make_gc<vector<GCPtr<MyObject>>>();
            // gcptr_vec->reserve(100000);
            GCPtr<MyObject> obj11 = gc::make_gc<MyObject>();
            obj10->d = obj11;
            obj11->d = obj10;
            for (int j = 0; j < 100000; j++) {
                GCPtr<MyObject> temp_obj = gc::make_gc<MyObject>();
                temp_obj->addH();
                if (rand() % 7 == 0) {
                    obj9 = temp_obj;
#if WITH_STL_TEST
                    gcptr_vec->push_back(obj9);
                    int idx = rand() % gcptr_vec->size();
                    GCPtr<MyObject> f = gcptr_vec->at(idx);
                    f->b = f->a;
#endif
                }
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
            obj11->b = (double)obj10->a / 2;
        }
        obj10->b = obj10->d->a * 2;

        auto end_time = chrono::steady_clock::now();
        long long duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
        cout << "User thread duration: " << duration << " ms" << endl;
        time_ += duration;

#if MULTITHREAD_TEST
        for (int tid = 0; tid < th_num; tid++)
            th[tid].join();
#endif

        if (triggerGC) {
            gc::triggerGC();
        }

        Sleep(100);
    }
    if (testDKThread) {
        dk_th.join();
    } else {
        dijkstra = nullptr;
        lruTest = nullptr;
    }
    if (triggerGC) {
        gc::triggerGC();
#if ENABLE_FREE_RESERVED
        gc::freeReservedMemory();
#endif
    }

    Sleep(1000);
    cout << "Average user thread duration: " << (double)time_ / (double)n << " ms" << endl;
    Sleep(1000);

    return 0;
}