#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#ifdef WIN32
#  undef setjmp
#  if !defined(__BORLANDC__)
#    define setjmp(x) _setjmp(x)
#  endif
#endif
//XXX

#ifdef WIN32
#  include <windows.h>
   /* Supposed to be in Winbase.h */
#  ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#    define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#  endif
#  include <win32thread.h>
#else
#  ifdef OS2
typedef perl_os_thread pthread_t;
#  else
#    include <pthread.h>
#  endif
#  include <thread.h>
#  define PERL_THREAD_SETSPECIFIC(k,v) pthread_setspecific(k,v)
#  ifdef OLD_PTHREADS_API
#    define PERL_THREAD_DETACH(t) pthread_detach(&(t))
#  else
#    define PERL_THREAD_DETACH(t) pthread_detach((t))
#  endif
#endif
#if !defined(HAS_GETPAGESIZE) && defined(I_SYS_PARAM)
#  include <sys/param.h>
#endif

#ifndef WIN32
STATIC int S_set_sigmask(sigset_t *);
#endif

enum node_type { STRING = 1, STORABLE = 2, QUEUE = 3 };

typedef struct message_queue message_queue;

typedef struct {
	enum node_type type;
	union {
		struct {
			char* ptr;
			STRLEN length;
		} string;
		message_queue* queue;
	};
} message;

typedef struct queue_node {
	message message;
	struct queue_node* next;
} queue_node;

struct message_queue {
	perl_mutex mutex;
	perl_cond condvar;
	queue_node* front;
	queue_node* back;
	queue_node* reserve;
	UV refcnt;
};

message_queue* queue_new() {
	message_queue* queue;
	Newxz(queue, 1, message_queue);
	MUTEX_INIT(&queue->mutex);
	COND_INIT(&queue->condvar);
	queue->refcnt = 1;
	return queue;
}

message_queue* S_get_queue_from(pTHX_ SV* queue_obj) {
	MAGIC* magic;
	if (!SvROK(queue_obj) || !SvMAGICAL(SvRV(queue_obj)) || !(magic = mg_find(SvRV(queue_obj), PERL_MAGIC_ext)))
		Perl_croak(aTHX_ "Something is very wrong, this is not a magic object\n");
	return (message_queue*)magic->mg_ptr;
}

#define get_queue_from(obj) S_get_queue_from(aTHX_ obj)

void queue_addref(message_queue* queue) {
	MUTEX_LOCK(&queue->mutex);
	++queue->refcnt;
	MUTEX_UNLOCK(&queue->mutex);
}

void queue_delref(message_queue* queue) {
	MUTEX_LOCK(&queue->mutex);
	--queue->refcnt;
	MUTEX_UNLOCK(&queue->mutex);
}

void S_queue_enqueue(pTHX_ message_queue* queue, SV* args) {
	message message;

	if (SvROK(args) && sv_isa(args, "threads::lite")) {
		Perl_croak(aTHX_ "Passing thread handles is not yet supported\n");
	}
	else if (SvROK(args) && sv_isa(args, "threads::lite::queue")) {
		message.type = QUEUE;
		message.queue = get_queue_from(args);
		queue_addref(message.queue);
	}
	else {
		if (!SvOK(args) || SvTYPE(args) > SVt_PVMG || SvROK(args)) {
			dSP;
			SAVETMPS;
			PUSHMARK(SP);
			PUSHs(sv_2mortal(newRV_noinc((SV*)args)));
			PUTBACK;
			call_pv("Storable::mstore", G_SCALAR);
			SPAGAIN;
			args = POPs;
			message.type = STORABLE;
		}
		else
			message.type = STRING;

		char* string = SvPV(args, message.string.length);
		message.string.ptr = savepvn(string, message.string.length);
	}

	MUTEX_LOCK(&queue->mutex);

	queue_node* new_entry;
	if (queue->reserve) {
		new_entry      = queue->reserve;
		queue->reserve = queue->reserve->next;
	}
	else
		Newx(new_entry, 1, queue_node);

	Copy(&message, &new_entry->message, 1, message);
	new_entry->next = NULL;

	if (queue->back != NULL) {
		queue->back->next = new_entry;
		queue->back       = queue->back->next;
	}
	else {
		queue->back = new_entry;
	}
	if( queue->front == NULL)
		queue->front = queue->back;

	COND_SIGNAL(&queue->condvar);
	MUTEX_UNLOCK(&queue->mutex);
}

#define queue_enqueue(queue, args) S_queue_enqueue(aTHX_ queue, args)

static MGVTBL table = { 0 };

SV* S_queue_object_new(pTHX_ message_queue* queue, HV* stash) {
	SV* val = newSV_type(SVt_PVMG);
	sv_magicext(val, NULL, PERL_MAGIC_ext, &table, (char*)queue, 0);
	SV* ret = newRV_noinc(val);
	sv_bless(ret, stash);
	return ret;
}

#define queue_object_new(queue, stash) S_queue_object_new(aTHX_ queue, stash)

SV* S_deserialize(pTHX_ message* message) {
	if (message->type == STRING || message->type == STORABLE) {
		SV* stored = newSV_type(SVt_PV);
		SvPVX(stored) = message->string.ptr;
		SvLEN(stored) = SvCUR(stored) = message->string.length;
		SvPOK_only(stored);
		
		if (message->type == STORABLE) {
			dSP;
			sv_2mortal(stored);
			PUSHMARK(SP);
			XPUSHs(stored);
			PUTBACK;
			call_pv("Storable::thaw", G_SCALAR);
			SPAGAIN;
			return SvRV(POPs);
		}
		else
			return stored;
	}
	else if (message->type == QUEUE) {
		return queue_object_new(message->queue, gv_stashpv("threads::lite::queue", FALSE));
	}
	else {
		Perl_croak(aTHX_ "Type %d is not yet implemented", message->type);
	}
}

#define deserialize(stored) S_deserialize(aTHX_ stored)

SV* S_queue_dequeue(pTHX_ message_queue* queue) {
	message message;
	MUTEX_LOCK(&queue->mutex);

	while (!queue->front) {
		warn("waiting for queue %p\n", queue);
		COND_WAIT(&queue->condvar, &queue->mutex);
	}

	Copy(&queue->front->message, &message, 1, message);

	if (queue->back == queue->front)
		queue->back = NULL;

	queue_node* new_front = queue->front->next;
	queue->front->next    = queue->reserve;
	queue->reserve        = queue->front;
	queue->front          = new_front;

	MUTEX_UNLOCK(&queue->mutex);
	return deserialize(&message);
}

#define queue_dequeue(queue) S_queue_dequeue(aTHX_ queue)

SV* S_queue_dequeue_nb(pTHX_ message_queue* queue) {
	message message;
	SV* ret = NULL;

	MUTEX_LOCK(&queue->mutex);

	if(queue->front) {
		Copy(&queue->front->message, &message, 1, message);

		if (queue->back == queue->front)
			queue->back = NULL;

		queue_node* new_front = queue->front->next;
		queue->front->next    = queue->reserve;
		queue->reserve        = queue->front;
		queue->front          = new_front;
		ret = TRUE;
		ret = deserialize(&message);
	}
	
	MUTEX_UNLOCK(&queue->mutex);

	return ret;
}

#define queue_dequeue_nb(queue) S_queue_dequeue_nb(aTHX_ queue)

void S_push_queued(pTHX_ SV* values) {
	dSP;
	
	if (SvTYPE(values) == SVt_PVAV) {
		if (GIMME_V == G_SCALAR) {
			SV** ret = av_fetch((AV*)values, 0, FALSE);
			PUSHs(ret ? sv_2mortal(SvREFCNT_inc(*ret)) : &PL_sv_undef);
		}
		else if (GIMME_V == G_ARRAY) {
			UV count = av_len((AV*)values) + 1;
			Copy(AvARRAY((AV*)values), SP + 1, count, SV*);
			SP += count;
		}
	}
	else {
		PUSHs(values);
	}
	PUTBACK;
}

#define push_queued(values) STMT_START { PUTBACK; S_push_queued(aTHX_ values); SPAGAIN; } STMT_END

static struct {
	perl_mutex lock;
	bool inited;
	UV count;
} global;

typedef struct {
	PerlInterpreter *interp;    /* The threads interpreter */
	message_queue* queue;

#ifdef WIN32
	DWORD  thr;                 /* OS's idea if thread id */
	HANDLE handle;              /* OS's waitable handle */
#else
	pthread_t thr;              /* OS's handle for the thread */
	sigset_t initial_sigmask;   /* Thread wakes up with signals blocked */
#endif
	IV stack_size;
	SV *err;                    /* Error from abnormally terminated thread */
	char *err_class;            /* Error object's classname if applicable */
} mthread;

void boot_DynaLoader(pTHX_ CV* cv);

static void xs_init(pTHX) {
	dXSUB_SYS;
	newXS((char*)"DynaLoader::boot_DynaLoader", boot_DynaLoader, (char*)__FILE__);
}

static const char* argv[] = {"", "-e", "threads::lite::_run()"};
static int argc = sizeof argv / sizeof *argv;

void* run_thread(void* arg) {
	MUTEX_LOCK(&global.lock);
	++global.count;
	MUTEX_UNLOCK(&global.lock);

	mthread* thread = (mthread*) arg;
	PerlInterpreter* my_perl = perl_alloc();
	thread->interp = my_perl;
	perl_construct(my_perl);
	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;

	perl_parse(my_perl, xs_init, argc, (char**)argv, NULL);
	load_module(PERL_LOADMOD_DENY, newSVpv("threads::lite", 0), NULL, NULL);
	S_set_sigmask(&thread->initial_sigmask);

	dSP;
	SAVETMPS;
	PUSHMARK(SP);
	HV* stash = gv_stashpv("threads::lite::queue", FALSE);
	PUSHs(sv_2mortal(queue_object_new(thread->queue, stash)));
	PUTBACK;
	call_pv("threads::lite::_run", G_VOID);
	SPAGAIN;
	FREETMPS;

	MUTEX_LOCK(&global.lock);
	--global.count;
	MUTEX_UNLOCK(&global.lock);
	return NULL;
}

static int S_mthread_hook(pTHX) {
	MUTEX_LOCK(&global.lock);
	int ret = global.count;
	MUTEX_UNLOCK(&global.lock);
	return ret;
}

#ifndef WIN32
/* Block most signals for calling thread, setting the old signal mask to
 * oldmask, if it is not NULL */
STATIC int
S_block_most_signals(sigset_t *oldmask)
{
	sigset_t newmask;

	sigfillset(&newmask);
	/* Don't block certain "important" signals (stolen from mg.c) */
#ifdef SIGILL
	sigdelset(&newmask, SIGILL);
#endif
#ifdef SIGBUS
	sigdelset(&newmask, SIGBUS);
#endif
#ifdef SIGSEGV
	sigdelset(&newmask, SIGSEGV);
#endif

#if defined(VMS)
	/* no per-thread blocking available */
	return sigprocmask(SIG_BLOCK, &newmask, oldmask);
#else
	return pthread_sigmask(SIG_BLOCK, &newmask, oldmask);
#endif /* VMS */
}

/* Set the signal mask for this thread to newmask */
STATIC int
S_set_sigmask(sigset_t *newmask)
{
#if defined(VMS)
	return sigprocmask(SIG_SETMASK, newmask, NULL);
#else
	return pthread_sigmask(SIG_SETMASK, newmask, NULL);
#endif /* VMS */
}
#endif /* WIN32 */

UV create_thread(message_queue* queue, IV stack_size) {
	mthread* thread;
	Newxz(thread, 1, mthread);
	thread->queue = queue;
	thread->stack_size = stack_size;
#ifdef WIN32
	thread->handle = CreateThread(NULL,
								  (DWORD)thread->stack_size,
								  run_thread,
								  (LPVOID)thread,
								  STACK_SIZE_PARAM_IS_A_RESERVATION,
								  &thread->thr);
#else
    int rc_stack_size = 0;
    int rc_thread_create = 0;

	S_block_most_signals(&thread->initial_sigmask);

	STATIC pthread_attr_t attr;
	STATIC int attr_inited = 0;
	STATIC int attr_joinable = PTHREAD_CREATE_JOINABLE;
	if (! attr_inited) {
		pthread_attr_init(&attr);
		attr_inited = 1;
	}

#  ifdef PTHREAD_ATTR_SETDETACHSTATE
	/* Threads start out joinable */
	PTHREAD_ATTR_SETDETACHSTATE(&attr, attr_joinable);
#  endif

#  ifdef _POSIX_THREAD_ATTR_STACKSIZE
	/* Set thread's stack size */
	if (thread->stack_size > 0) {
		rc_stack_size = pthread_attr_setstacksize(&attr, (size_t)thread->stack_size);
	}
#  endif

	/* Create the thread */
	if (! rc_stack_size) {
#  ifdef OLD_PTHREADS_API
		rc_thread_create = pthread_create(&thread->thr, attr, run_thread, (void *)thread);
#  else
#	if defined(HAS_PTHREAD_ATTR_SETSCOPE) && defined(PTHREAD_SCOPE_SYSTEM)
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#	endif
		rc_thread_create = pthread_create(&thread->thr, &attr, run_thread, (void *)thread);
#  endif
	}
	/* Now it's safe to accept signals, since we're in our own interpreter's
	 * context and we have created the thread.
	 */
	S_set_sigmask(&thread->initial_sigmask);
#endif

}

MODULE = threads::lite             PACKAGE = threads::lite

PROTOTYPES: DISABLED

BOOT:
    PL_threadhook = &S_mthread_hook;

	if (!global.inited) {
		MUTEX_INIT(&global.lock);
		global.inited = TRUE;
		global.count = 1;
	}
	sv_setiv(get_sv("Storable::Deparse", TRUE | GV_ADDMULTI), 1);
	sv_setiv(get_sv("Storable::Eval",    TRUE | GV_ADDMULTI), 1);


void
_start(object, stack_size)
	SV* object;
	UV stack_size;
	CODE:
		SV** queue_obj = hv_fetch((HV*)SvRV(object), "queue", 5, FALSE);
		if (!queue_obj)
			Perl_croak(aTHX_ "No queue available\n");
		message_queue* queue = get_queue_from(*queue_obj);
		create_thread(queue, stack_size);
		
MODULE = threads::lite             PACKAGE = threads::lite::queue

PROTOTYPES: DISABLED

SV*
new(class)
	SV* class;
	PPCODE:
	message_queue* queue = queue_new();
	SV* ret = queue_object_new(queue, gv_stashsv(class, FALSE));
	PUSHs(sv_2mortal(ret));

void
enqueue(object, ...)
	SV* object;
	CODE:
		message_queue* queue = get_queue_from(object);
		if (items == 1)
			Perl_croak(aTHX_ "Can't enqueue empty list\n");
		SV* args = (items > 2) ? (SV*)av_make(items - 1, PL_stack_base + ax + 1) : ST(1);
		queue_enqueue(queue, args);
		FREETMPS;

void
dequeue(object)
	SV* object;
	PPCODE:
		message_queue* queue = get_queue_from(object);
		SV* values = queue_dequeue(queue);
		push_queued(values);

void
dequeue_nb(object)
	SV* object;
	PPCODE:
		message_queue* queue = get_queue_from(object);
		SV* values = queue_dequeue_nb(queue);
		if (values)
			push_queued(values);
		else
			XSRETURN_EMPTY;
