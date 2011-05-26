typedef message queue_node;

typedef struct {
	perl_mutex mutex;
	perl_cond condvar;
	message* front;
	message* back;
} message_queue;

void queue_init(message_queue*);
void S_queue_enqueue(pTHX_ message_queue* queue, const message* message, perl_mutex* lock);
#define queue_enqueue(queue, message, lock) S_queue_enqueue(aTHX_ queue, message, lock)
const message* S_queue_dequeue(pTHX_ message_queue* queue, perl_mutex* lock);
#define queue_dequeue(queue, lock) S_queue_dequeue(aTHX_ queue, lock)
const message* S_queue_dequeue_nb(pTHX_ message_queue* queue, perl_mutex* lock);
#define queue_dequeue_nb(queue, lock) S_queue_dequeue_nb(aTHX_ queue, lock)
void S_queue_destroy(pTHX_ message_queue*);
#define queue_destroy(queue) S_queue_destroy(aTHX_ queue)
