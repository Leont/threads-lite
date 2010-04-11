#include <stdlib.h>
#include "sync.h"

#define atomic_add(address, value) __sync_add_and_fetch(address, value)
#define atomic_sub(address, value) __sync_sub_and_fetch(address, value)
#define atomic_replace(address, old_value, new_value) __sync_bool_compare_and_swap(address, old_value, new_value)

void spin_lock(spin_lock_t* lock) {
	do {
		while (lock->count);
	} while (!atomic_replace(&lock->count, 0, 1));
}

void spin_unlock(spin_lock_t* lock) {
	atomic_replace(&lock->count, 1, 0);
}

void lock_shared(shared_lock_t* lock) {
	int value;
	redo:
		value = lock->count;
	if (value < 0)
		goto redo;
	if (!atomic_replace(&lock->count, value, value+1))
		goto redo;
}

void unlock_shared(shared_lock_t* lock) {
	atomic_sub(&lock->count, 1);
}

void lock_exclusive(shared_lock_t* lock) {
	int value;
	redo:
		value = lock->count;
	if (value > 0)
		goto redo;
	if (!atomic_replace(&lock->count, 0, -1))
		goto redo;
}

void unlock_exclusive(shared_lock_t* lock) {
	if (!atomic_replace(&lock->count, -1, 0))
		abort();
}
