#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#define NO_XSLOCKS
#include "XSUB.h"

#include "message.h"
#include "queue.h"
#include "mthread.h"
#include "resources.h"

/*
 * !!! global data !!!
 */

bool inited = 0;

typedef struct {
	perl_mutex lock;
	UV current;
	UV allocated;
	void** objects;
} resource;

static S_resource_init(resource* res, UV preallocate) {
	MUTEX_INIT(&res->lock);
	res->objects = safemalloc(preallocate * sizeof(void*));
	res->allocated = preallocate;
}

#define resource_init(res, pre) S_resource_init(res, pre)

static UV resource_addobject(resource* res, void* object) {
	MUTEX_LOCK(&res->lock);
	UV ret = res->current;
	if (res->current == res->allocated)
		res->objects = saferealloc(res->objects, sizeof(void*) * (res->allocated *=2));
	res->objects[res->current++] = object;
	MUTEX_UNLOCK(&res->lock);
	return ret;
}

static resource threads;
static resource queues;

static struct {
	perl_mutex mutex;
	perl_cond condvar;
	int count;
} counter;

static void wait_for_all_other_threads() {
	MUTEX_LOCK(&counter.mutex);
	while (counter.count > 1) 
		COND_WAIT(&counter.condvar, &counter.mutex);

	MUTEX_UNLOCK(&counter.mutex);
	MUTEX_DESTROY(&counter.mutex);
	COND_DESTROY(&counter.condvar);
}

static XS(end_locker) {
	dVAR; dXSARGS;

	wait_for_all_other_threads();

	perl_mutex* mutex = get_shutdown_mutex();
	MUTEX_LOCK(mutex);
	XSRETURN_EMPTY;
}

static void end_unlocker() {
	perl_mutex* mutex = get_shutdown_mutex();
	MUTEX_UNLOCK(mutex);
}

void global_init(pTHX) {
	if (!inited) {
		inited = TRUE;

		MUTEX_INIT(&counter.mutex);
		COND_INIT(&counter.condvar);
		counter.count = 0;

		resource_init(&threads, 8);
		resource_init(&queues, 8);
		mthread* ret = mthread_alloc(aTHX);
		store_self(aTHX, ret);

		/* This is a nasty trick to make sure locking is performed during part of the destruct */
		newXS("END", end_locker, __FILE__);
		atexit(end_unlocker);
	}
}

mthread* mthread_alloc(PerlInterpreter* my_perl) {
	mthread* ret;

	MUTEX_LOCK(&counter.mutex);
	counter.count++;
	MUTEX_UNLOCK(&counter.mutex);

	Newxz(ret, 1, mthread);
	queue_init(&ret->queue);
	UV id = resource_addobject(&threads, ret);
	ret->id = id;
	ret->interp = my_perl;
	MUTEX_INIT(&ret->lock);
	return ret;
}

void mthread_destroy(mthread* thread) {
	MUTEX_LOCK(&threads.lock);
	threads.objects[thread->id] = NULL;
	queue_destroy(&thread->queue);
	MUTEX_UNLOCK(&threads.lock);

	MUTEX_DESTROY(&thread->lock);

	MUTEX_LOCK(&counter.mutex);
	counter.count--;
	COND_SIGNAL(&counter.condvar);
	MUTEX_UNLOCK(&counter.mutex);
}

static inline mthread* S_get_thread(pTHX_ UV thread_id) {
	if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
		Perl_croak(aTHX_  "Thread %"UVuf" doesn't exist", thread_id);
	return threads.objects[thread_id];
}

#define get_thread(id) S_get_thread(aTHX_ id)

UV queue_alloc(IV linked_to) {
	message_queue* queue;
	Newxz(queue, 1, message_queue);
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

	MUTEX_LOCK(&threads.lock);
	THREAD_TRY {
		mthread* thread = get_thread(thread_id);
		queue_enqueue(&thread->queue, message, &threads.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&threads.lock) );
}

void S_queue_send(pTHX_ UV queue_id, message* message) {
	dXCPT;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_enqueue(queue, message, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );
}

void S_queue_receive(pTHX_ UV queue_id, message* message) {
	dXCPT;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_dequeue(queue, message, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );
}

void S_queue_receive_nb(pTHX_ UV queue_id, message* message) {
	dXCPT;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_dequeue_nb(queue, message, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );
}

void S_send_listeners(pTHX_ mthread* thread, message* mess) {
	dXCPT;

	MUTEX_LOCK(&thread->lock);
	int i;
	for (i = 0; i < thread->listeners.head; ++i) {
		message clone;;
		MUTEX_LOCK(&threads.lock); // unlocked by queue_enqueue
		UV thread_id = thread->listeners.list[i];
		if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
			continue;
		message_clone(mess, &clone);
		queue_enqueue(&((mthread*)threads.objects[thread_id])->queue, &clone, &threads.lock);
	}
	MUTEX_UNLOCK(&thread->lock);
}

#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)

void thread_add_listener(pTHX, UV talker, UV listener) {
	dXCPT;

	MUTEX_LOCK(&threads.lock);
	THREAD_TRY {
		mthread* thread = get_thread(talker);
		if (thread->listeners.alloc == thread->listeners.head) {
			thread->listeners.alloc = thread->listeners.alloc ? thread->listeners.alloc * 2 : 1;
			Renew(thread->listeners.list, thread->listeners.alloc, IV);
		}
		thread->listeners.list[thread->listeners.head++] = listener;
	} THREAD_FINALLY( MUTEX_UNLOCK(&threads.lock) );
}

