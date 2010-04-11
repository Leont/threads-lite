typedef struct {
	int count;
} spin_lock_t;

void spin_lock(spin_lock_t* lock);
void spin_unlock(spin_lock_t* lock);

typedef struct {
	int count;
} shared_lock_t;

void lock_shared(shared_lock_t* lock);
void unlock_shared(shared_lock_t* lock);
void lock_exclusive(shared_lock_t* lock);
void unlock_exclusive(shared_lock_t* lock);
