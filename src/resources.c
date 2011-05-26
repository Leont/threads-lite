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

static S_resource_init(pTHX_ resource* res, UV preallocate) {
	MUTEX_INIT(&res->lock);
	res->objects = PerlMemShared_calloc(preallocate, sizeof(void*));
	res->allocated = preallocate;
}

#define resource_init(res, pre) S_resource_init(aTHX_ res, pre)

static UV S_resource_addobject(pTHX_ resource* res, void* object) {
	UV ret;
	MUTEX_LOCK(&res->lock);
	ret = res->current;
	if (res->current == res->allocated)
		res->objects = PerlMemShared_realloc(res->objects, sizeof(void*) * (res->allocated *=2));
	res->objects[res->current++] = object;
	MUTEX_UNLOCK(&res->lock);
	return ret;
}
#define resource_addobject(res, obj) S_resource_addobject(aTHX_ res, obj)

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

static XSPROTO(end_locker) {
	dVAR; dXSARGS;
	perl_mutex* mutex;

	wait_for_all_other_threads();

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

		MUTEX_INIT(&counter.mutex);
		COND_INIT(&counter.condvar);
		counter.count = 0;

		resource_init(&threads, 8);
		resource_init(&queues, 8);
		ret = mthread_alloc(aTHX);
		ret->interp = my_perl;
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

	ret = PerlMemShared_calloc(1, sizeof *ret);
	queue_init(&ret->queue);
	ret->id = resource_addobject(&threads, ret);
	ret->interp = NULL;
	MUTEX_INIT(&ret->lock);
	return ret;
}

void mthread_destroy(mthread* thread) {
	MUTEX_LOCK(&threads.lock);

	PerlInterpreter* my_perl = thread->interp;
	threads.objects[thread->id] = NULL;
	queue_destroy(&thread->queue);
	MUTEX_UNLOCK(&threads.lock);

	MUTEX_DESTROY(&thread->lock);

	MUTEX_LOCK(&counter.mutex);
	counter.count--;
	COND_SIGNAL(&counter.condvar);
	MUTEX_UNLOCK(&counter.mutex);
}

static mthread* S_get_thread(pTHX_ UV thread_id) {
	if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
		Perl_croak(aTHX_  "Thread %"UVuf" doesn't exist", thread_id);
	return threads.objects[thread_id];
}

#define get_thread(id) S_get_thread(aTHX_ id)

UV S_queue_alloc(pTHX_ IV linked_to) {
	message_queue* queue;
	queue = PerlMemShared_calloc(1, sizeof *queue);
	queue_init(queue);
	return resource_addobject(&queues, queue);
}

static message_queue* S_get_queue(pTHX_ UV queue_id) {
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


void S_thread_send(pTHX_ UV thread_id, const message* message) {
	dXCPT;

	MUTEX_LOCK(&threads.lock);
	THREAD_TRY {
		mthread* thread = get_thread(thread_id);
		queue_enqueue(&thread->queue, message, &threads.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&threads.lock) );
}

void S_queue_send(pTHX_ UV queue_id, const message* message) {
	dXCPT;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		queue_enqueue(queue, message, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );
}

const message* S_queue_receive(pTHX_ UV queue_id) {
	dXCPT;
	const message* ret;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		ret = queue_dequeue(queue, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );

	return ret;
}

const message* S_queue_receive_nb(pTHX_ UV queue_id) {
	dXCPT;
	const message* ret;

	MUTEX_LOCK(&queues.lock);
	THREAD_TRY {
		message_queue* queue = get_queue(queue_id);
		ret = queue_dequeue_nb(queue, &queues.lock);
	} THREAD_CATCH( MUTEX_UNLOCK(&queues.lock) );
	return ret;
}

void S_send_listeners(pTHX_ mthread* thread, const message* mess) {
	int i;
	dXCPT;

	MUTEX_LOCK(&thread->lock);
	for (i = 0; i < thread->listeners.head; ++i) {
		const message* clone;
		UV thread_id;
		MUTEX_LOCK(&threads.lock); /* unlocked by queue_enqueue */
		thread_id = thread->listeners.list[i];
		if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
			continue;
		clone = message_clone(mess);
		queue_enqueue(&((mthread*)threads.objects[thread_id])->queue, clone, &threads.lock);
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
			thread->listeners.list = PerlMemShared_realloc(thread->listeners.list, sizeof(IV) * thread->listeners.alloc);
		}
		thread->listeners.list[thread->listeners.head++] = listener;
	} THREAD_FINALLY( MUTEX_UNLOCK(&threads.lock) );
}

