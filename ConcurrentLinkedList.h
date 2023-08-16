#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include "Iterator.h"

template<typename T>
class ConcurrentLinkedList {
private:
    class Node {
    public:
        std::optional<T> dat;
        std::atomic<std::shared_ptr<Node>> next;

        Node() : dat(std::nullopt), next(nullptr) {}

        explicit Node(T e) : dat(e), next(nullptr) {}

        Node(T e, std::shared_ptr<Node> _next) : dat(e), next(_next) {}
    };

    const std::shared_ptr<Node> head;

public:
    ConcurrentLinkedList() : head(std::make_shared<Node>()) {
    }

    ~ConcurrentLinkedList() = default;

    void push_head(T e) {
        std::shared_ptr<Node> E = std::make_shared<Node>(e);
        while (true) {
            std::shared_ptr<Node> original_head = head->next.load();
            E->next = original_head;
            if (head->next.compare_exchange_weak(original_head, E))
                break;
        }
    }

    std::optional<T> get_head() const {
        auto _head = head->next.load();
        if (_head == nullptr) return std::nullopt;
        else return _head->dat;
    }

    std::optional<T> poll_head() {
        while (true) {
            std::shared_ptr<Node> original_head = head->next;
            if (original_head == nullptr) return std::nullopt;
            if (head->next.compare_exchange_weak(original_head, original_head->next))
                return original_head->dat;
        }
    }

    bool empty() const {
        return this->head->next.load() == nullptr;
    }

    bool remove(T e) {
        std::shared_ptr<Node> c = head;
        while (true) {
            std::shared_ptr<Node> c_next = c->next.load();
            if (c_next == nullptr) break;
            if (c_next->dat == e) {
                if (c->next.compare_exchange_weak(c_next, c_next->next.load()))
                    return true;
            } else {
                c = c->next;
            }
        }
        return false;
    }

    class LinkedListIterator : public RemovableIterator<T> {
    private:
        std::shared_ptr<Node> c;
        std::shared_ptr<Node> c_next;
        ConcurrentLinkedList& linkedList;

    public:
        explicit LinkedListIterator(ConcurrentLinkedList& linkedList_) : linkedList(linkedList_) {
            c = nullptr;
            c_next = nullptr;
        }

        T current() const override {
            return c_next->dat.value();
        }

        bool MoveNext() override {
            if (c == nullptr) {
                c = linkedList.head;
            } else {
                if (c_next == c->next.load())
                    c = c->next;
            }
            if (c == nullptr) return false;
            c_next = c->next;
            if (c_next == nullptr) return false;
            return true;
        }

        bool remove() override {
            if (c == nullptr || c_next == nullptr) return false;
            if (c->next.compare_exchange_weak(c_next, c_next->next.load())) {
                return true;
            } else {
                return false;
            }
        }

        bool remove(T e) override {
            std::shared_ptr<Node> c = this->c;
            while (true) {
                std::shared_ptr<Node> c_next = c->next.load();
                if (c_next == nullptr) break;
                if (c_next->dat == e) {
                    if (c->next.compare_exchange_weak(c_next, c_next->next.load()))
                        return true;
                } else {
                    c = c->next;
                }
            }
            return false;
        }
    };

    std::unique_ptr<RemovableIterator<T>> getRemovableIterator() {
        return std::make_unique<LinkedListIterator>(*this);
    }

    std::unique_ptr<Iterator<T>> getIterator() const {
        return std::make_unique<LinkedListIterator>(const_cast<ConcurrentLinkedList&>(*this));
    }
};