#include <stdlib.h>
#include "sync.h"

#define atomic_add(address, value) __sync_add_and_fetch(address, value)
#define atomic_sub(address, value) __sync_sub_and_fetch(address, value)
#define atomic_replace(address, old_value, new_value) __sync_bool_compare_and_swap(address, old_value, new_value)

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

void lock_init(shared_lock_t* lock) {
	lock->count = 0;
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

void counter_init(atomic_counter* counter, int value) {
	counter->counter = value;
	sem_init(&counter->waiter, 0, 0);
}
void counter_inc(atomic_counter* counter) {
	atomic_add(&counter->counter, 1);
}
void counter_dec(atomic_counter* counter) {
	if (atomic_sub(&counter->counter, 1) == 0)
		sem_post(&counter->waiter);
}
void counter_wait(atomic_counter* counter) {
	if (atomic_sub(&counter, 1) != 0)
		sem_wait(&counter->waiter);
}
void counter_destroy(atomic_counter* counter) {
	sem_destroy(&counter->waiter);
}
