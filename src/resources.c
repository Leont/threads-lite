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

void resource_removeobject(resource* res, UV id) {
	MUTEX_LOCK(&res->lock);
	res->objects[id] = NULL;
	MUTEX_UNLOCK(&res->lock);
}

resource threads;
resource queues;

void global_init(pTHX) {
	if (!inited) {
		inited = TRUE;
		resource_init(&threads, 8, mthread*);
		mthread* ret = mthread_alloc();
#  ifdef WIN32
		ret->thr = GetCurrentThreadId();
#  else
		ret->thr = pthread_self();
#  endif
		store_self(ret);
	}
}

mthread* mthread_alloc(IV linked_to) {
	mthread* ret;
	Newxz(ret, 1, mthread);
	queue_init(&ret->queue);
	if (linked_to >= 0) {
		Newxz(ret->listeners.list, 1, IV);
		ret->listeners.list[0] = linked_to;
		ret->listeners.alloc = ret->listeners.head = 1;
	}
	UV id = resource_addobject(&threads, ret);
	ret->id = id;
	return ret;
}

void mthread_destroy(mthread* thread) {
	resource_removeobject(&threads, thread->id);
	queue_destroy(&thread->queue);
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

#define get_queue(id) S_get_thread(aTHX_ id)

#define THREAD_TRY \
	XCPT_TRY_START

#define THREAD_CATCH(undo) \
	XCPT_TRY_END;\
	XCPT_CATCH { undo; XCPT_RETHROW; }

#define THREAD_FINALLY(undo) \
	XCPT_TRY_END;\
	undo;\
	XCPT_CATCH { XCPT_RETHROW; }


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

void S_thread_add_listener(pTHX_ UV talker, UV listener) {
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

