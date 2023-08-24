#pragma once

#include <atomic>
#include <thread>
#include "IReadWriteLock.h"

class WeakSpinReadWriteLock : public IReadWriteLock {
private:
	std::atomic<int> read_cnt;
	std::atomic<bool> write_flag;
public:
	WeakSpinReadWriteLock() : read_cnt(0), write_flag(false) {
	}

	void lockRead() override {
		while (write_flag.load(std::memory_order_acquire)) {}
		read_cnt.fetch_add(1, std::memory_order_release);
	}

	void unlockRead() override {
		read_cnt.fetch_add(-1, std::memory_order_release);
	}

	void lockWrite(bool yield) override {
		while (true) {
			int c_read_cnt = read_cnt.load(std::memory_order_acquire);
			bool c_write_flag = write_flag.load(std::memory_order_acquire);
			if (c_read_cnt || c_write_flag) {
				if (yield) std::this_thread::yield();
				continue;
			}
			if (write_flag.compare_exchange_weak(c_write_flag, true, std::memory_order_release)) {
				while (read_cnt.load(std::memory_order_acquire) != 0) {}
				break;
			}
		}
	}

	void lockWrite() override {
		lockWrite(false);
	}

	void unlockWrite() override {
		write_flag.store(false, std::memory_order_release);
	}
};