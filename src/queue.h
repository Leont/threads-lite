typedef struct queue_node queue_node;

typedef struct {
	perl_mutex mutex;
	perl_cond condvar;
	queue_node* front;
	queue_node* back;
	queue_node* reserve;
} message_queue;

void queue_init(message_queue*);
void S_queue_enqueue(pTHX_ message_queue* queue, message* message, perl_mutex* lock);
#define queue_enqueue(queue, message, lock) S_queue_enqueue(aTHX_ queue, message, lock)
void S_queue_dequeue(pTHX_ message_queue* queue, message* message, perl_mutex* lock);
#define queue_dequeue(queue, message, lock) S_queue_dequeue(aTHX_ queue, message, lock)
bool S_queue_dequeue_nb(pTHX_ message_queue* queue, message* message, perl_mutex* lock);
#define queue_dequeue_nb(queue, message, lock) S_queue_dequeue_nb(aTHX_ queue, message, lock)
void S_queue_destroy(pTHX_ message_queue*);
#define queue_destroy(queue) S_queue_destroy(aTHX_ queue)
