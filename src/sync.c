#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "sync.h"

readwrite* readwrite_new() {
    readwrite* ret;
    Newxz(ret, 1, readwrite);
    MUTEX_INIT(&ret->lock);
    COND_INIT(&ret->read_var);
    COND_INIT(&ret->write_var);
	return ret;
}

semaphore* semaphore_new(IV value) {
	semaphore* ret;
	Newxz(ret, 1, semaphore);
	MUTEX_INIT(&ret->lock);
	COND_INIT(&ret->cond);
	ret->value = value;
	return ret;
}

void semaphore_up(semaphore* sem) {
	MUTEX_LOCK(&sem->lock);
	if (++sem->value > 0)
		COND_BROADCAST(&sem->cond);
	MUTEX_UNLOCK(&sem->lock);
}

void semaphore_down(semaphore* sem) {
	MUTEX_LOCK(&sem->lock);
	while (sem->value <= 0)
		COND_WAIT(&sem->cond, &sem->lock);
	--sem->value;
	MUTEX_UNLOCK(&sem->lock);
}

IV semaphore_value(semaphore* sem) {
	MUTEX_LOCK(&sem->lock);
	IV ret = sem->value;
	MUTEX_UNLOCK(&sem->lock);
	return ret;
}
