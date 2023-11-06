#include "MutexReadWriteLock.h"

thread_local int MutexReadWriteLock::read_locked_cnt = 0;

void MutexReadWriteLock::lockRead() {
	if (read_locked_cnt == 0)
		sharedMutex.lock_shared();
	++read_locked_cnt;
}

void MutexReadWriteLock::unlockRead() {
	if (read_locked_cnt <= 0)
		throw std::runtime_error("MutexReadWriteLock: read_locked_cnt is 0");
	--read_locked_cnt;
	if (read_locked_cnt == 0)
		sharedMutex.unlock_shared();
}

void MutexReadWriteLock::lockWrite() {
	sharedMutex.lock();
}

void MutexReadWriteLock::lockWrite(bool) {
	sharedMutex.lock();
}

void MutexReadWriteLock::unlockWrite() {
	sharedMutex.unlock();
}