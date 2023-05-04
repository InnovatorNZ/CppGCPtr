#pragma once

class IReadWriteLock {
public:
    IReadWriteLock() = default;

    IReadWriteLock(const IReadWriteLock&) = delete;

    IReadWriteLock(IReadWriteLock&&) = delete;

    IReadWriteLock& operator=(const IReadWriteLock&) = delete;

    virtual ~IReadWriteLock() = default;

    virtual void lockRead() = 0;

    virtual void unlockRead() = 0;

    virtual void lockWrite() = 0;

    virtual void lockWrite(bool yield) = 0;

    virtual void unlockWrite() = 0;
};