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

void global_init() {
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
		queue_init(&system_queue);
	}
}

mthread* mthread_alloc() {
	mthread* ret;
	Newxz(ret, 1, mthread);
	queue_init(&ret->queue);
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
		Perl_croak(aTHX_  "Thread "UVuf" doesn't exist", thread_id);
	return threads[thread_id];
}

#define get_thread(id) S_get_thread(aTHX_ id)

#define THREAD_TRY \
	XCPT_TRY_START

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

void S_thread_add_listener(pTHX_ UV talker, UV listener) {
	dXCPT;

	MUTEX_LOCK(&thread_lock);
	THREAD_TRY {
		mthread* thread = get_thread(talker);
		if (thread->listeners.alloc == thread->listeners.head) {
			thread->listeners.alloc = thread->listeners.alloc ? thread->listeners.alloc * 2 : 1;
			Renew(thread->listeners.list, thread->listeners.alloc, int);
		}
		thread->listeners.list[thread->listeners.head++] = listener;
	} THREAD_FINALLY( MUTEX_UNLOCK(&thread_lock) );
}

