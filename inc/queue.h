enum node_type { EMPTY = 0, STRING = 1, STORABLE = 2 };

typedef struct {
	enum node_type type;
	struct {
		char* ptr;
		STRLEN length;
	} string;
} message;

typedef struct queue_node queue_node;

typedef struct {
	perl_mutex mutex;
	perl_cond condvar;
	queue_node* front;
	queue_node* back;
	queue_node* reserve;
} message_queue;

void S_message_push_stack(pTHX_ message*);
#define message_push_stack(values) STMT_START { PUTBACK; S_message_push_stack(aTHX_ (values)); SPAGAIN; } STMT_END

void S_message_pull_stack(pTHX_ message*);
#define message_pull_stack(message) STMT_START { PUTBACK; S_message_pull_stack(aTHX_ message); SPAGAIN; } STMT_END


void queue_init(message_queue*);
void queue_destroy(message_queue*);
void queue_enqueue(message_queue* queue, message* message, perl_mutex* lock);
void S_queue_enqueue_copy(pTHX_ message_queue* queue, message* message, perl_mutex* lock);
void queue_dequeue(message_queue* queue, message* message);
bool queue_dequeue_nb(message_queue* queue, message* message);

#define queue_enqueue_copy(queue, message, lock) S_queue_enqueue_copy(aTHX_ queue, message, lock)
