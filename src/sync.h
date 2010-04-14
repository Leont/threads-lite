typedef struct {
	volatile long count;
} spin_lock_t;

void spin_init(spin_lock_t* lock);
void spin_lock(spin_lock_t* lock);
void spin_unlock(spin_lock_t* lock);

typedef struct _rw_lock_t {
	volatile long count;
} rw_lock_t;

void lock_init(rw_lock_t* lock);
void lock_shared(rw_lock_t* lock);
void unlock_shared(rw_lock_t* lock);
void lock_exclusive(rw_lock_t* lock);
void unlock_exclusive(rw_lock_t* lock);

typedef struct {
	volatile long counter;
} atomic_counter;

void counter_init(atomic_counter*, long);
long counter_inc(atomic_counter*);
long counter_dec(atomic_counter*);
