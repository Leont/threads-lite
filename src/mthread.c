#ifndef USE_SAFE
#define USE_SAFE 1
#endif

#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#if !USE_SAFE
#define NO_XSLOCKS
#endif
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
	SV *thread_sv, *self;
	AV* message_cache;

	thread_sv = newSV_type(SVt_PV);
	SvPVX(thread_sv) = (char*) thread;
	SvCUR(thread_sv) = sizeof(mthread);
	SvLEN(thread_sv) = 0;
	SvPOK_only(thread_sv);
	SvREADONLY_on(thread_sv);
	hv_store(PL_modglobal, "threads::lite::thread", 21, thread_sv, 0);

	self = newRV_noinc(newSVuv(thread->id));
	sv_bless(self, gv_stashpv("threads::lite::tid", TRUE));
	hv_store(PL_modglobal, "threads::lite::self", 19, self, 0);

	message_cache = newAV();
	hv_store(PL_modglobal, "threads::lite::message_cache", 28, (SV*)message_cache, 0);
	thread->cache = message_cache;
}

mthread* S_get_self(pTHX) {
	SV** self_sv = hv_fetch(PL_modglobal, "threads::lite::thread", 21, FALSE);
	if (!self_sv) {
		mthread* ret;
		if (ckWARN(WARN_THREADS))
			Perl_warn(aTHX, "Creating thread context where non existed\n");
		ret = mthread_alloc(aTHX);
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
	message to_run, message;
	SV *call, *status;
	perl_mutex* shutdown_mutex;

	dSP;

#ifndef WIN32
	S_set_sigmask(&thread->initial_sigmask);
#endif
	PERL_SET_CONTEXT(my_perl);

	queue_dequeue(&thread->queue, &to_run, NULL);

	ENTER;
	SAVETMPS;
	call = SvRV(message_load_value(&to_run));

	PUSHMARK(SP);
	XPUSHs(sv_2mortal(newSVpvn("exit", 4)));
	status = sv_2mortal(newSVpvn("normal", 6));
	XPUSHs(status);
	XPUSHs(sv_2mortal(newSViv(thread->id)));

	ENTER;
	PUSHMARK(SP);
	PUTBACK;
	call_sv(call, G_ARRAY|G_EVAL);
	SPAGAIN;

	if (SvTRUE(ERRSV)) {
		sv_setpvn(status, "error", 5);
		warn("Thread %"UVuf" got error %s\n", thread->id, SvPV_nolen(ERRSV));
		PUSHs(ERRSV);
	}

	message_from_stack_pushed(&message);
	LEAVE;

	send_listeners(thread, &message);
	message_destroy(&message);

	FREETMPS;
	LEAVE;

	shutdown_mutex = get_shutdown_mutex();

	MUTEX_LOCK(shutdown_mutex);
	perl_destruct(my_perl);
	MUTEX_UNLOCK(shutdown_mutex);

	mthread_destroy(thread);

	PerlMemShared_free(thread);

	perl_free(my_perl);

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
	static pthread_attr_t attr;
	static int attr_inited = 0;
	static int attr_detached = PTHREAD_CREATE_DETACHED;

	int rc_stack_size = 0;
	int rc_thread_create = 0;
	pthread_t thr;

	S_block_most_signals(&thread->initial_sigmask);

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
	if (stack_size > 0)
		rc_stack_size = pthread_attr_setstacksize(&attr, (size_t)stack_size);
#  endif

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

static void save_modules(pTHX, message* message, HV* options) {
	SV** modules_ptr = hv_fetch(options, "modules", 7, FALSE);
	if (modules_ptr && SvROK(*modules_ptr) && SvTYPE(SvRV(*modules_ptr)) == SVt_PVAV)
		message_store_value(message, SvRV(*modules_ptr));
	else
		Zero(message, 1, message);
}

static void load_modules(pTHX, message* list_mess) {
	if (list_mess->type) {
		SV* list_ref;
		AV* list;
		I32 len;
		int i;

		SAVETMPS;
		list_ref = message_load_value(list_mess);
		SvREFCNT_inc(list_ref);
		list = (AV*)SvRV(list_ref);
		len = av_len(list) + 1;
		for(i = 0; i < len; i++) {
			SV** entry = av_fetch(list, i, FALSE);
			load_module(PERL_LOADMOD_NOIMPORT, *entry, NULL, NULL);
		}
		FREETMPS;
	}
}

static void push_thread(pTHX, mthread* thread) {
	PERL_SET_CONTEXT(aTHX);
	{
		dSP;
		SV* to_push = newRV_noinc(newSVuv(thread->id));
		sv_bless(to_push, gv_stashpv("threads::lite::tid", FALSE));
		XPUSHs(to_push);
		PUTBACK;
	}
}

struct thread_create {
	UV parent_id;
	message to_run;
	message modules;
	int monitor;
	size_t stack_size;
};

static IV get_iv_option(pTHX_ HV* options, const char* key, IV default_value) {
	SV** value__ptr = hv_fetch(options, key, strlen(key), FALSE);
	if (value__ptr && SvOK(*value__ptr))
		return SvIV(*value__ptr);
	return default_value;
}

static int prepare_thread_create(pTHX, struct thread_create* new_thread, HV* options, SV* startup) {
	UV id = get_self()->id;

	message_store_value(&new_thread->to_run, startup);

	save_modules(aTHX, &new_thread->modules, options);

	new_thread->monitor = get_iv_option(aTHX, options, "monitor", FALSE);
	new_thread->stack_size = get_iv_option(aTHX, options, "stack_size", 65536);
	return get_iv_option(aTHX, options, "pool_size", 1);
}

#if !USE_SAFE
static PerlInterpreter* thread_clone(pTHX, mthread* thread) {
	dXCPT;
	XCPT_TRY_START {
		return perl_clone(my_perl, 0);
	} XCPT_TRY_END;
	XCPT_CATCH {
		Perl_warn(aTHX_ "Cought exception, rethrowing\n");
		mthread_destroy(thread);
		XCPT_RETHROW;
	}
}
#endif

void S_create_push_threads(tTHX self, HV* options, SV* startup) {
	struct thread_create thread_options;
	int clone_number;
	int counter;

	Zero(&thread_options, 1, struct thread_create);
	clone_number = prepare_thread_create(self, &thread_options, options, startup);

#if USE_SAFE
	for (counter = 0; counter < clone_number; ++counter) {
#else
	{
#endif /* USE_SAFE */
		PerlInterpreter* my_perl;
		mthread* thread;
		message to_run;

		my_perl = construct_perl();

		thread = mthread_alloc(my_perl);
		store_self(my_perl, thread);

		if (thread_options.monitor)
			thread_add_listener(self, thread->id, thread_options.parent_id);

		if (thread_options.modules.type) {
			message modules;
			message_clone(&thread_options.modules, &modules);
			load_modules(my_perl, &modules);
		}

		push_thread(self, thread);

#if !USE_SAFE
		while (--clone_number) {
			PerlInterpreter* my_clone = thread_clone(my_perl, thread);
			mthread* thread = mthread_alloc(my_clone);
			message clone;

			store_self(my_clone, thread);
			if (thread_options.monitor)
				thread_add_listener(self, thread->id, thread_options.parent_id);
			message_clone(&thread_options.to_run, &clone);
			queue_enqueue(&thread->queue, &clone, NULL);
			push_thread(self, thread);
			start_thread(thread, thread_options.stack_size);
		}
#endif
		message_clone(&thread_options.to_run, &to_run);
		queue_enqueue(&thread->queue, &to_run, NULL);
		start_thread(thread, thread_options.stack_size);
	}
	PERL_SET_CONTEXT(self);

	S_message_destroy(self, &thread_options.to_run);
	S_message_destroy(self, &thread_options.modules);
}
