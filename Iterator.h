#pragma once

template<typename T>
class Iterator {
public:
    Iterator() = default;

    virtual ~Iterator() = default;

    virtual T current() const = 0;

    virtual bool MoveNext() = 0;
};