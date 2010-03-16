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

void S_message_push_stack(pTHX_ message*, U32 context);
#define message_push_stack(values, context) STMT_START { PUTBACK; S_message_push_stack(aTHX_ (values), context); SPAGAIN; } STMT_END

void S_message_pull_stack(pTHX_ message*);
#define message_pull_stack_pushed(message) STMT_START { PUTBACK; S_message_pull_stack(aTHX_ message); SPAGAIN; } STMT_END
#define message_pull_stack(message, offset) STMT_START { PUSHMARK(offset); message_pull_stack_pushed(message); } STMT_END

void S_message_store_value(pTHX_ message* message, SV* value);
#define message_store_value(message, value) S_message_store_value(aTHX_ message, value)

SV* S_message_load_value(pTHX_ message* message);
#define message_load_value(message) S_message_load_value(aTHX_ message)

void queue_init(message_queue*);
void queue_destroy(message_queue*);
void queue_enqueue(message_queue* queue, message* message, perl_mutex* lock);
void S_queue_enqueue_copy(pTHX_ message_queue* queue, message* message, perl_mutex* lock);
#define queue_enqueue_copy(queue, message, lock) S_queue_enqueue_copy(aTHX_ queue, message, lock)
void queue_dequeue(message_queue* queue, message* message);
bool queue_dequeue_nb(message_queue* queue, message* message);

