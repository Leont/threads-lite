#include <semaphore.h>

#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#define NO_XSLOCKS
#include "XSUB.h"

#include "message.h"
#include "sync.h"
#include "queue.h"
#include "mthread.h"
#include "resources.h"

/*
 * !!! global data !!!
 */

bool inited = 0;

typedef struct {
	rw_lock_t lock;
	UV current;
	UV allocated;
	void** objects;
} resource;

static S_resource_init(resource* res, UV preallocate) {
	lock_init(&res->lock);
	res->objects = PerlMemShared_calloc(preallocate, sizeof(void*));
	res->allocated = preallocate;
}

#define resource_init(res, pre) S_resource_init(res, pre)

static UV resource_addobject(resource* res, void* object) {
	UV ret = res->current;
	lock_exclusive(&res->lock);
	if (res->current == res->allocated)
		res->objects = PerlMemShared_realloc(res->objects, sizeof(void*) * (res->allocated *=2));
	res->objects[res->current++] = object;
	unlock_exclusive(&res->lock);
	return ret;
}

static resource threads;
static resource queues;

static struct {
	atomic_counter counter;
	sem_t semaphore;
} thread_waiter;

static void wait_for_all_other_threads(mthread* self) {
	mthread_destroy(self);
	sem_wait(&thread_waiter.semaphore);
	sem_destroy(&thread_waiter.semaphore);
}

static XS(end_locker) {
	dVAR; dXSARGS;
	perl_mutex* mutex;

	mthread* self = get_self();
	wait_for_all_other_threads(self);

	mutex = get_shutdown_mutex();
	MUTEX_LOCK(mutex);
	XSRETURN_EMPTY;
}

static void end_unlocker() {
	perl_mutex* mutex = get_shutdown_mutex();
	MUTEX_UNLOCK(mutex);
}

void global_init(pTHX) {
	if (!inited) {
		mthread* ret;

		inited = TRUE;

		counter_init(&thread_waiter.counter, 0);
		sem_init(&thread_waiter.semaphore, 0, 0);

		resource_init(&threads, 8);
		resource_init(&queues, 8);
		ret = mthread_alloc(aTHX);
		store_self(aTHX, ret);

		/* This is a nasty trick to make sure locking is performed during part of the destruct */
		newXS("END", end_locker, __FILE__);
		atexit(end_unlocker);
	}
}

mthread* mthread_alloc(PerlInterpreter* my_perl) {
	mthread* ret;

	counter_inc(&thread_waiter.counter);

	ret = PerlMemShared_calloc(1, sizeof *ret);
	queue_init(&ret->queue);
	ret->id = resource_addobject(&threads, ret);
	ret->interp = my_perl;
	spin_init(&ret->lock);
	return ret;
}

void mthread_destroy(mthread* thread) {
	lock_exclusive(&threads.lock);
	threads.objects[thread->id] = NULL;
	queue_destroy(&thread->queue);
	unlock_exclusive(&threads.lock);

	if (counter_dec(&thread_waiter.counter) == 0)
		sem_post(&thread_waiter.semaphore);
}

static inline mthread* S_get_thread(pTHX_ UV thread_id) {
	if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
		Perl_croak(aTHX_  "Thread %"UVuf" doesn't exist", thread_id);
	return threads.objects[thread_id];
}

#define get_thread(id) S_get_thread(aTHX_ id)

UV queue_alloc(IV linked_to) {
	message_queue* queue;
	queue = PerlMemShared_calloc(1, sizeof *queue);
	queue_init(queue);
	return resource_addobject(&queues, queue);
}

static inline message_queue* S_get_queue(pTHX_ UV queue_id) {
	if (queue_id >= queues.current || queues.objects[queue_id] == NULL)
		Perl_croak(aTHX_  "queue %"UVuf" doesn't exist", queue_id);
	return queues.objects[queue_id];
}

#define get_queue(id) S_get_queue(aTHX_ id)

#define THREAD_TRY \
	XCPT_TRY_START

#define THREAD_CATCH_FINALLY(catch, finally) \
	XCPT_TRY_END;\
	finally;\
	XCPT_CATCH { catch; XCPT_RETHROW; }

#define THREAD_CATCH(undo) THREAD_CATCH_FINALLY(undo, 0)

#define THREAD_FINALLY(undo) THREAD_CATCH_FINALLY(0, undo)


void S_thread_send(pTHX_ UV thread_id, message* message) {
	dXCPT;

	lock_shared(&threads.lock);
	THREAD_TRY {
		mthread* thread = get_thread(thread_id);
		queue_enqueue(&thread->queue, message, &threads.lock);
	} THREAD_CATCH( unlock_shared(&threads.lock) );
}

void S_queue_send(pTHX_ UV queue_id, message* message) {
	dXCPT;

	lock_shared(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_enqueue(queue, message, &queues.lock);
	} THREAD_CATCH( unlock_shared(&queues.lock) );
}

void S_queue_receive(pTHX_ UV queue_id, message* message) {
	dXCPT;

	lock_shared(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_dequeue(queue, message, &queues.lock);
	} THREAD_CATCH( unlock_shared(&queues.lock) );
}

void S_queue_receive_nb(pTHX_ UV queue_id, message* message) {
	dXCPT;

	lock_shared(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_dequeue_nb(queue, message, &queues.lock);
	} THREAD_CATCH( unlock_shared(&queues.lock) );
}

void S_send_listeners(pTHX_ mthread* thread, message* mess) {
	int i;
	dXCPT;

	spin_lock(&thread->lock);
	for (i = 0; i < thread->listeners.head; ++i) {
		message clone;
		UV thread_id;
		lock_shared(&threads.lock); // unlocked by queue_enqueue
		thread_id = thread->listeners.list[i];
		if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
			continue;
		message_clone(mess, &clone);
		queue_enqueue(&((mthread*)threads.objects[thread_id])->queue, &clone, &threads.lock);
	}
	spin_unlock(&thread->lock);
}

#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)

void thread_add_listener(pTHX, UV talker, UV listener) {
	dXCPT;

	lock_shared(&threads.lock);
	THREAD_TRY {
		mthread* thread = get_thread(talker);
		if (thread->listeners.alloc == thread->listeners.head) {
			thread->listeners.alloc = thread->listeners.alloc ? thread->listeners.alloc * 2 : 1;
			thread->listeners.list = PerlMemShared_realloc(thread->listeners.list, sizeof(IV) * thread->listeners.alloc);
		}
		thread->listeners.list[thread->listeners.head++] = listener;
	} THREAD_FINALLY( unlock_shared(&threads.lock) );
}

