typedef struct {
	int count;
} spin_lock_t;

void spin_init(spin_lock_t* lock);
void spin_lock(spin_lock_t* lock);
void spin_unlock(spin_lock_t* lock);

typedef struct _shared_lock_t {
	int count;
} shared_lock_t;

void lock_init(shared_lock_t* lock);
void lock_shared(shared_lock_t* lock);
void unlock_shared(shared_lock_t* lock);
void lock_exclusive(shared_lock_t* lock);
void unlock_exclusive(shared_lock_t* lock);

typedef struct {
	int counter;
} atomic_counter;

void counter_init(atomic_counter*, int);
int counter_inc(atomic_counter*);
int counter_dec(atomic_counter*);
