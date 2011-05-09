#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"

#include "message.h"
#include "queue.h"

/*
 * Message queues
 */

struct queue_node {
	const message* message;
	struct queue_node* next;
};

static void node_unshift(queue_node** position, queue_node* new_node) {
	new_node->next = *position;
	*position = new_node;
}

static queue_node* node_shift(queue_node** position) {
	queue_node* ret = *position;
	*position = (*position)->next;
	return ret;
}

static void node_push(queue_node** end, queue_node* new_node) {
	queue_node** cur = end;
	while(*cur)
		cur = &(*cur)->next;
	*end = *cur = new_node;
	new_node->next = NULL;
}

static void S_node_destroy(pTHX_ struct queue_node** current) {
	while (*current != NULL) {
		struct queue_node** next = &(*current)->next;
		if ((*current)->message)
			destroy_message((*current)->message);
		PerlMemShared_free(*current);
		*current = NULL;
		current = next;
	}
}
#define node_destroy(current) S_node_destroy(aTHX_ current)

void queue_init(message_queue* queue) {
	Zero(queue, 1, message_queue);
	MUTEX_INIT(&queue->mutex);
	COND_INIT(&queue->condvar);
}

void S_queue_enqueue(pTHX_ message_queue* queue, const message* message_, perl_mutex* external_lock) {
	queue_node* new_entry;
	MUTEX_LOCK(&queue->mutex);
	if (external_lock)
		MUTEX_UNLOCK(external_lock);

	if (queue->reserve)
		new_entry = node_shift(&queue->reserve);
	else
		new_entry = PerlMemShared_calloc(1, sizeof *new_entry);

	new_entry->message = message_;
	new_entry->next = NULL;

	node_push(&queue->back, new_entry);
	if (queue->front == NULL)
		queue->front = queue->back;

	COND_SIGNAL(&queue->condvar);
	MUTEX_UNLOCK(&queue->mutex);
}

static const message* queue_shift(message_queue* queue) {
	queue_node* front = node_shift(&queue->front);
	const message* ret = front->message;
	front->message = NULL;
	node_unshift(&queue->reserve, front);

	if (queue->front == NULL)
		queue->back = NULL;
	return ret;
}

const message* S_queue_dequeue(pTHX_ message_queue* queue, perl_mutex* external_lock) {
	const message* ret;
	MUTEX_LOCK(&queue->mutex);
	if (external_lock)
		MUTEX_UNLOCK(external_lock);

	while (!queue->front)
		COND_WAIT(&queue->condvar, &queue->mutex);

	ret = queue_shift(queue);
	MUTEX_UNLOCK(&queue->mutex);

	return ret;
}

const message* S_queue_dequeue_nb(pTHX_ message_queue* queue, perl_mutex* external_lock) {
	MUTEX_LOCK(&queue->mutex);
	if (external_lock)
		MUTEX_UNLOCK(external_lock);

	if (queue->front) {
		const message* ret = queue_shift(queue);

		MUTEX_UNLOCK(&queue->mutex);
		return ret;
	}
	else {
		MUTEX_UNLOCK(&queue->mutex);
		return NULL;
	}
}

void S_queue_destroy(pTHX_ message_queue* queue) {
	MUTEX_LOCK(&queue->mutex);
	node_destroy(&queue->front);
	node_destroy(&queue->reserve);
	COND_DESTROY(&queue->condvar);
	MUTEX_UNLOCK(&queue->mutex);
	MUTEX_DESTROY(&queue->mutex);
}
