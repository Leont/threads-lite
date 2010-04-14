#include <stdlib.h>
#include "sync.h"

#if defined _MSC_VER
#define atomic_inc(address) InterlockedIncrement(address)
#define atomic_dec(address) InterlockedDecrement(address)
#define atomic_replace(address, old_value, new_value) ( InterlockedCompareExchange(address, new_value, old_value) == old_value )
#elif defined __GNUC__
#define atomic_inc(address) __sync_add_and_fetch(address, 1)
#define atomic_dec(address) __sync_sub_and_fetch(address, 1)
#define atomic_replace(address, old_value, new_value) __sync_bool_compare_and_swap(address, old_value, new_value)
#else
#error Unsupported compiler
#endif

void spin_init(spin_lock_t* lock) {
	lock->count = 0;
}
void spin_lock(spin_lock_t* lock) {
	do {
		while (lock->count);
	} while (!atomic_replace(&lock->count, 0, 1));
}

void spin_unlock(spin_lock_t* lock) {
	atomic_replace(&lock->count, 1, 0);
}

void lock_init(rw_lock_t* lock) {
	lock->count = 0;
}
void lock_shared(rw_lock_t* lock) {
	long value;
	redo:
		value = lock->count;
	if (value < 0)
		goto redo;
	if (!atomic_replace(&lock->count, value, value+1))
		goto redo;
}

void unlock_shared(rw_lock_t* lock) {
	atomic_dec(&lock->count);
}

void lock_exclusive(rw_lock_t* lock) {
	long value;
	redo:
		value = lock->count;
	if (value > 0)
		goto redo;
	if (!atomic_replace(&lock->count, 0, -1))
		goto redo;
}

void unlock_exclusive(rw_lock_t* lock) {
	if (!atomic_replace(&lock->count, -1, 0))
		abort();
}

void counter_init(atomic_counter* counter, long value) {
	counter->counter = value;
}
long counter_inc(atomic_counter* counter) {
	return atomic_inc(&counter->counter);
}
long counter_dec(atomic_counter* counter) {
	return atomic_dec(&counter->counter);
}
