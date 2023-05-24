#pragma once

template<typename T>
class Iterator {
public:
    Iterator() = default;

    virtual ~Iterator() = default;

    virtual T next() = 0;

    virtual bool hasNext() = 0;
};