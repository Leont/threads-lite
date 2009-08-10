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
UV count = 0;
struct mthread** threads = NULL;

void global_init() {
	if (!inited) {
		MUTEX_INIT(&lock);
		inited = TRUE;
		Newxz(threads, 8, mthread*);
		mthread_alloc();
	}
}

mthread* mthread_alloc() {
	mthread* ret;
	Newxz(ret, 1, mthread);
	ret->queue = queue_new();
	MUTEX_LOCK(&lock);
	threads[count++] = ret;
	MUTEX_UNLOCK(&lock);
	return ret;
}

void mthread_finish(mthread* thread) {
	MUTEX_LOCK(&lock);
	--count;
	MUTEX_UNLOCK(&lock);
}
