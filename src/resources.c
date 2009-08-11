#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "queue.h"
#include "mthread.h"
#include "resources.h"

/*
 * !!! global data !!!
 */

perl_mutex lock;
bool inited = 0;
UV current = 0;
UV allocated = 0;
struct mthread** threads = NULL;

void global_init() {
	if (!inited) {
		MUTEX_INIT(&lock);
		inited = TRUE;
		Newxz(threads, 8, mthread*);
		allocated = 8;
		mthread_alloc();
	}
}

mthread* mthread_alloc() {
	mthread* ret;
	Newxz(ret, 1, mthread);
	queue_init(&ret->queue);
	MUTEX_LOCK(&lock);
	ret->thread_id = current;
	if (current == allocated)
		Renew(threads, allocated *=2, mthread*);
	threads[current++] = ret;
	MUTEX_UNLOCK(&lock);
	return ret;
}

void mthread_destroy(mthread* thread) {
	MUTEX_LOCK(&lock);
	threads[thread->thread_id] = NULL;
	MUTEX_UNLOCK(&lock);
	queue_destroy(&thread->queue);
}

bool thread_send(UV thread_id, message* message) {
	MUTEX_LOCK(&lock);
	bool ret = thread_id < current && threads[thread_id] != NULL;
	if (ret)
		queue_enqueue(&threads[thread_id]->queue, message, &lock);
	else
		MUTEX_UNLOCK(&lock);
	return ret;
}
