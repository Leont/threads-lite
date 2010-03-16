#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#define NO_XSLOCKS
#include "XSUB.h"

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
	size_t size;
} resource;

S_resource_init(resource* res, UV preallocate, size_t size) {
	MUTEX_INIT(&res->lock);
	res->size = size;
	res->objects = safemalloc(preallocate * size);
	res->allocated = preallocate;
}

#define resource_init(res, pre, type) S_resource_init(res, pre, sizeof(type))

UV resource_addobject(resource* res, void* object) {
	MUTEX_LOCK(&res->lock);
	UV ret = res->current;
	if (res->current == res->allocated)
		res->objects = saferealloc(res->objects, res->size * (res->allocated *=2));
	res->objects[res->current++] = object;
	MUTEX_UNLOCK(&res->lock);
	return ret;
}

resource threads;
resource queues;

void global_init(pTHX) {
	if (!inited) {
		inited = TRUE;
		resource_init(&threads, 8, mthread*);
		mthread* ret = mthread_alloc(aTHX);
#  ifdef WIN32
		ret->thr = GetCurrentThreadId();
#  else
		ret->thr = pthread_self();
#  endif
		ret->interp = NULL; // XXX
		store_self(aTHX, ret);
	}
}

mthread* mthread_alloc(PerlInterpreter* my_perl) {
	mthread* ret;
	Newxz(ret, 1, mthread);
	queue_init(&ret->queue);
	UV id = resource_addobject(&threads, ret);
	ret->id = id;
	ret->interp = my_perl;
	return ret;
}

void mthread_destroy(mthread* thread) {
	MUTEX_LOCK(&threads.lock);
	threads.objects[thread->id] = NULL;
	queue_destroy(&thread->queue);
	MUTEX_UNLOCK(&threads.lock);
}

static mthread* S_get_thread(pTHX_ UV thread_id) {
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

void S_send_listeners(pTHX_ mthread* thread, message* message) {
	dXCPT;

	MUTEX_LOCK(&thread->lock);
	int i;
	for (i = 0; i < thread->listeners.head; ++i) {
		MUTEX_LOCK(&threads.lock);
		UV thread_id = thread->listeners.list[i];
		if (thread_id >= threads.current || threads.objects[thread_id] == NULL)
			continue;
		queue_enqueue_copy(&((mthread*)threads.objects[thread_id])->queue, message, &threads.lock);
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

