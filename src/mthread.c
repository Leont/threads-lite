#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "message.h"
#include "queue.h"
#include "mthread.h"
#include "resources.h"

/*
 * Some definitions that were copied verbatim froms threads.xs, these need to be looked at
 */

#ifdef WIN32
#  undef setjmp
#  if !defined(__BORLANDC__)
#    define setjmp(x) _setjmp(x)
#  endif
#endif

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
static int S_set_sigmask(sigset_t *);
#endif

/*
 * Threads implementation itself
 */

void boot_DynaLoader(pTHX_ CV* cv);

static void xs_init(pTHX) {
	dXSUB_SYS;
	newXS((char*)"DynaLoader::boot_DynaLoader", boot_DynaLoader, (char*)__FILE__);
}

static const char* argv[] = {"", "-e", "0"};
static int argc = sizeof argv / sizeof *argv;

void store_self(pTHX, mthread* thread) {
	SV* thread_sv = newSV_type(SVt_PV);
	SvPVX(thread_sv) = (char*) thread;
	SvCUR(thread_sv) = sizeof(mthread);
	SvLEN(thread_sv) = 0;
	SvPOK_only(thread_sv);
	SvREADONLY_on(thread_sv);
	hv_store(PL_modglobal, "threads::lite::thread", 21, thread_sv, 0);

	SV* self = newRV_noinc(newSVuv(thread->id));
	sv_bless(self, gv_stashpv("threads::lite::tid", TRUE));
	hv_store(PL_modglobal, "threads::lite::self", 19, self, 0);

	AV* message_cache = newAV();
	hv_store(PL_modglobal, "threads::lite::message_cache", 28, (SV*)message_cache, 0);
	thread->cache = message_cache;
}

mthread* S_get_self(pTHX) {
	SV** self_sv = hv_fetch(PL_modglobal, "threads::lite::thread", 21, FALSE);
	if (!self_sv) {
		if (ckWARN(WARN_THREADS))
			Perl_warn(aTHX, "Creating thread context where non existed\n");
		mthread* ret = mthread_alloc(aTHX);
		store_self(aTHX, ret);
		return ret;
	}
	return (mthread*)SvPV_nolen(*self_sv);
}

perl_mutex* get_shutdown_mutex() {
	static int inited = 0;
	static perl_mutex mutex;
	if (!inited) {
		MUTEX_INIT(&mutex);
		inited = 1;
	}
	return &mutex;
}

static void* run_thread(void* arg) {
	mthread* thread = (mthread*) arg;
	PerlInterpreter* my_perl = thread->interp;

#ifndef WIN32
	S_set_sigmask(&thread->initial_sigmask);
#endif
	PERL_SET_CONTEXT(my_perl);

	message to_run;
	queue_dequeue(&thread->queue, &to_run, NULL);
	SV* call = SvRV(message_load_value(&to_run));

	dSP;

	PUSHMARK(SP);
	PUSHs(newSVpvn("exit", 4));
	SV* status = newSVpvn("normal", 6);
	PUSHs(status);
	PUSHs(newSViv(thread->id));

	PUSHMARK(SP);
	PUTBACK;
	call_sv(call, G_ARRAY|G_EVAL);
	SPAGAIN;

	if (SvTRUE(ERRSV)) {
		sv_setpvn(status, "error", 5);
		warn("Thread %"UVuf" got error %s\n", thread->id, SvPV_nolen(ERRSV));
		PUSHs(ERRSV);
	}
	message message;
	message_from_stack_pushed(&message);
	send_listeners(thread, &message);
	message_destroy(&message);

	perl_mutex* shutdown_mutex = get_shutdown_mutex();
	MUTEX_LOCK(shutdown_mutex);
	perl_destruct(my_perl);
	MUTEX_UNLOCK(shutdown_mutex);

	mthread_destroy(thread);
	perl_free(my_perl);
	
	Safefree(thread);

	return NULL;
}

#ifndef WIN32
/* Block most signals for calling thread, setting the old signal mask to
 * oldmask, if it is not NULL */
static int S_block_most_signals(sigset_t *oldmask)
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
static int S_set_sigmask(sigset_t *newmask)
{
#if defined(VMS)
	return sigprocmask(SIG_SETMASK, newmask, NULL);
#else
	return pthread_sigmask(SIG_SETMASK, newmask, NULL);
#endif /* VMS */
}
#endif /* WIN32 */


static mthread* start_thread(mthread* thread, IV stack_size) {
#ifdef WIN32
	CreateThread(NULL, (DWORD)stack_size, run_thread, (LPVOID)thread, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
#else
	int rc_stack_size = 0;
	int rc_thread_create = 0;

	S_block_most_signals(&thread->initial_sigmask);

	static pthread_attr_t attr;
	static int attr_inited = 0;
	static int attr_detached = PTHREAD_CREATE_DETACHED;
	if (! attr_inited) {
		pthread_attr_init(&attr);
		attr_inited = 1;
	}

#  ifdef PTHREAD_ATTR_SETDETACHSTATE
	/* Threads start out detached */
	PTHREAD_ATTR_SETDETACHSTATE(&attr, attr_detached);
#  endif

#  ifdef _POSIX_THREAD_ATTR_STACKSIZE
	/* Set thread's stack size */
	if (stack_size > 0) {
		rc_stack_size = pthread_attr_setstacksize(&attr, (size_t)stack_size);
	}
#  endif

	pthread_t thr;
	/* Create the thread */
	if (! rc_stack_size) {
#  ifdef OLD_PTHREADS_API
		rc_thread_create = pthread_create(&thr, attr, run_thread, (void *)thread);
#  else
#	if defined(HAS_PTHREAD_ATTR_SETSCOPE) && defined(PTHREAD_SCOPE_SYSTEM)
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
#	endif
		rc_thread_create = pthread_create(&thr, &attr, run_thread, (void *)thread);
#  endif
	}
	/* Now it's safe to accept signals, since we're in our own interpreter's
	 * context and we have created the thread.
	 */
	S_set_sigmask(&thread->initial_sigmask);
#endif
	return thread;
}

static PerlInterpreter* construct_perl() {
	PerlInterpreter* my_perl = perl_alloc();
	PERL_SET_CONTEXT(my_perl);
	perl_construct(my_perl);
	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;

	perl_parse(my_perl, xs_init, argc, (char**)argv, NULL);
	ENTER;
	load_module(PERL_LOADMOD_NOIMPORT, newSVpv("threads::lite", 0), NULL, NULL);
	LEAVE;
	return my_perl;
}

static int get_clone_number(pTHX, HV* options) {
	SV** clone_number_ptr = hv_fetch(options, "pool_size", 9, FALSE);
	if (clone_number_ptr && SvOK(*clone_number_ptr))
		return SvIV(*clone_number_ptr);
	return 1;
}

static int should_monitor(pTHX, HV* options) {
	SV** monitor_ptr = hv_fetch(options, "monitor", 7, FALSE);
	if (monitor_ptr && SvOK(*monitor_ptr))
		return SvIV(*monitor_ptr);
	return FALSE;
}

static void load_modules(pTHX, HV* options) {
	SV** modules_ptr = hv_fetch(options, "modules", 7, FALSE);
	if (modules_ptr && SvROK(*modules_ptr) && SvTYPE(SvRV(*modules_ptr)) == SVt_PVAV) {
		AV* list = (AV*)SvRV(*modules_ptr);
		I32 len = av_len(list) + 1;
		int i;
		for(i = 0; i < len; i++) {
			SV** entry = av_fetch(list, i, FALSE);
			load_module(PERL_LOADMOD_NOIMPORT, *entry, NULL, NULL);
		}
	}
}

static unsigned get_stack_size(pTHX, HV* options) {
	SV** stack_size_ptr = hv_fetch(options, "stack_size", 10, FALSE);
	if (stack_size_ptr && SvOK(*stack_size_ptr))
		return SvUV(*stack_size_ptr);
	return 65536u;
}

static void push_thread(pTHX, mthread* thread) {
	PERL_SET_CONTEXT(aTHX);
	dSP;
	SV* to_push = newRV_noinc(newSVuv(thread->id));
	sv_bless(to_push, gv_stashpv("threads::lite::tid", FALSE));
	XPUSHs(to_push);
	PUTBACK;
}

void S_create_push_threads(PerlInterpreter* self, HV* options, SV* startup) {
	UV id = S_get_self(self)->id;
	message to_run;
	S_message_store_value(self, &to_run, startup);
	SvREFCNT_inc(options);

	int clone_number = get_clone_number(self, options);
	int monitor = should_monitor(self, options);
	size_t stack_size = get_stack_size(self, options);

	PerlInterpreter* my_perl = construct_perl();
	load_modules(my_perl, options);

	mthread* thread = mthread_alloc(my_perl);
	store_self(my_perl, thread);
	if (monitor)
		thread_add_listener(self, thread->id, id);

	queue_enqueue(&thread->queue, &to_run, NULL);

	push_thread(self, thread);
	while (--clone_number) {
		PerlInterpreter* my_clone = perl_clone(my_perl, 0);
		mthread* thread = mthread_alloc(my_clone);
		message clone;

		store_self(my_clone, thread);
		if (monitor)
			thread_add_listener(self, thread->id, id);
		message_clone(&to_run, &clone);
		queue_enqueue(&thread->queue, &clone, NULL);
		push_thread(self, thread);
		start_thread(thread, stack_size);
	}

	start_thread(thread, stack_size);

	PERL_SET_CONTEXT(self);
}
