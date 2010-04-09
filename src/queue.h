typedef struct queue_node queue_node;

typedef struct {
	perl_mutex mutex;
	perl_cond condvar;
	queue_node* front;
	queue_node* back;
	queue_node* reserve;
} message_queue;

void queue_init(message_queue*);
void queue_destroy(message_queue*);
void queue_enqueue(message_queue* queue, message* message, perl_mutex* lock);
void queue_dequeue(message_queue* queue, message* message, perl_mutex* lock);
bool queue_dequeue_nb(message_queue* queue, message* message, perl_mutex* lock);

