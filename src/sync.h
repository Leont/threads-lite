typedef struct {
	volatile long count;
} spin_lock_t;

void spin_init(spin_lock_t* lock);
void spin_lock(spin_lock_t* lock);
void spin_unlock(spin_lock_t* lock);

typedef struct _shared_lock_t {
	volatile long count;
} shared_lock_t;

void lock_init(shared_lock_t* lock);
void lock_shared(shared_lock_t* lock);
void unlock_shared(shared_lock_t* lock);
void lock_exclusive(shared_lock_t* lock);
void unlock_exclusive(shared_lock_t* lock);

typedef struct {
	volatile long counter;
} atomic_counter;

void counter_init(atomic_counter*, long);
long counter_inc(atomic_counter*);
long counter_dec(atomic_counter*);
