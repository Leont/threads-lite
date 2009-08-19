enum lock_mode { READ_LOCK, WRITE_LOCK };

typedef struct {
    perl_mutex lock;
    perl_cond read_var;
    perl_cond write_var;
    size_t readers;
	enum lock_mode mode;
} readwrite;

readwrite* readwrite_new();

typedef struct {
	perl_mutex lock;
    perl_cond cond;
	IV value;
} semaphore;

semaphore* semaphore_new(IV);
void semaphore_up(semaphore*);
void semaphore_down(semaphore*);
IV semaphore_value(semaphore*);
