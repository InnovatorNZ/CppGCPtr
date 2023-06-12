#pragma once

template<typename T>
class Iterator {
public:
    Iterator() = default;

    virtual ~Iterator() = default;

    virtual T current() const = 0;

    virtual bool MoveNext() = 0;
};

template<typename T>
class RemovableIterator : public Iterator<T> {
public:
    virtual bool remove() = 0;

    virtual bool remove(T) = 0;
};