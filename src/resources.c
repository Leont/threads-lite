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

perl_mutex thread_lock;
bool inited = 0;
UV current = 0;
UV allocated = 0;
mthread** threads = NULL;

void global_init(pTHX) {
	if (!inited) {
		MUTEX_INIT(&thread_lock);
		inited = TRUE;
		Newxz(threads, 8, mthread*);
		allocated = 8;
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
	MUTEX_LOCK(&thread_lock);
	ret->thread_id = current;
	if (current == allocated)
		Renew(threads, allocated *=2, mthread*);
	threads[current++] = ret;
	MUTEX_UNLOCK(&thread_lock);
	return ret;
}

void mthread_destroy(mthread* thread) {
	MUTEX_LOCK(&thread_lock);
	threads[thread->thread_id] = NULL;
	MUTEX_UNLOCK(&thread_lock);
	queue_destroy(&thread->queue);
}

static mthread* S_get_thread(pTHX_ UV thread_id) {
	if (thread_id >= current || threads[thread_id] == NULL)
		Perl_croak(aTHX_  "Thread %"UVuf" doesn't exist", thread_id);
	return threads[thread_id];
}

#define get_thread(id) S_get_thread(aTHX_ id)

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

	MUTEX_LOCK(&thread_lock);
	THREAD_TRY {
		mthread* thread = get_thread(thread_id);
		queue_enqueue(&thread->queue, message, &thread_lock);
	} THREAD_FINALLY( MUTEX_UNLOCK(&thread_lock) );
}

void S_send_listeners(pTHX_ mthread* thread, message* message) {
	dXCPT;

	MUTEX_LOCK(&thread->lock);
	int i;
	for (i = 0; i < thread->listeners.head; ++i) {
		MUTEX_LOCK(&thread_lock);
		UV thread_id = thread->listeners.list[i];
		if (thread_id >= current || threads[thread_id] == NULL)
			continue;
		queue_enqueue_copy(&threads[thread_id]->queue, message, &thread_lock);
	}
	MUTEX_UNLOCK(&thread->lock);
}

#define send_listeners(thread, message) S_send_listeners(aTHX_ thread, message)

void S_thread_add_listener(pTHX_ UV talker, UV listener) {
	dXCPT;

	MUTEX_LOCK(&thread_lock);
	THREAD_TRY {
		mthread* thread = get_thread(talker);
		if (thread->listeners.alloc == thread->listeners.head) {
			thread->listeners.alloc = thread->listeners.alloc ? thread->listeners.alloc * 2 : 1;
			Renew(thread->listeners.list, thread->listeners.alloc, IV);
		}
		thread->listeners.list[thread->listeners.head++] = listener;
	} THREAD_FINALLY( MUTEX_UNLOCK(&thread_lock) );
}

