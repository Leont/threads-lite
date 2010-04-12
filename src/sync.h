#include <semaphore.h>

typedef struct {
	int count;
} spin_lock_t;

void spin_init(spin_lock_t* lock);
void spin_lock(spin_lock_t* lock);
void spin_unlock(spin_lock_t* lock);

typedef struct {
	int count;
} shared_lock_t;

void lock_init(shared_lock_t* lock);
void lock_shared(shared_lock_t* lock);
void unlock_shared(shared_lock_t* lock);
void lock_exclusive(shared_lock_t* lock);
void unlock_exclusive(shared_lock_t* lock);

typedef struct {
	int counter;
	sem_t waiter;
} atomic_counter;

void counter_init(atomic_counter*, int);
void counter_inc(atomic_counter*);
void counter_dec(atomic_counter*);
void counter_wait(atomic_counter*);
void counter_destroy(atomic_counter*);
