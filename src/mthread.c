#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

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

static const char* argv[] = {"", "-e", "threads::lite::_run()"};
static int argc = sizeof argv / sizeof *argv;

void S_store_self(pTHX_ mthread* thread) {
	SV* thread_sv = newSV_type(SVt_PV);
	SvPVX(thread_sv) = (char*) thread;
	SvCUR(thread_sv) = sizeof(mthread);
	SvLEN(thread_sv) = 0;
	SvPOK_only(thread_sv);
	SvREADONLY_on(thread_sv);
	hv_store(PL_modglobal, "thread::lite::self", 18, thread_sv, 0);

}

static void* run_thread(void* arg) {
	mthread* thread = (mthread*) arg;
	PerlInterpreter* my_perl = perl_alloc();
	perl_construct(my_perl);
	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;

	perl_parse(my_perl, xs_init, argc, (char**)argv, NULL);
	S_set_sigmask(&thread->initial_sigmask);

	store_self(thread);

	load_module(PERL_LOADMOD_NOIMPORT, newSVpv("threads::lite", 0), NULL, NULL);

	dSP;

	PUSHMARK(SP);
	call_pv("threads::lite::_get_runtime", G_SCALAR);
	SPAGAIN;

	SV* call = POPs;
	SV* status = sv_2mortal(newSVpvn("normal", 6));
	PUSHs(status);
	SV** old = SP;

	PUSHMARK(SP);
	PUTBACK;
	call_sv(call, G_ARRAY|G_EVAL);
	SPAGAIN;

	message message;
	if (SvTRUE(ERRSV)) {
		sv_setpvn(status, "error", 5);
		PUSHs(ERRSV);
	}
	PUSHMARK(old);
	message_pull_stack(&message);
	send_listeners(thread, &message);
	message_destroy(&message);

	perl_destruct(my_perl);
	perl_free(my_perl);

	mthread_destroy(thread);
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

mthread* create_thread(IV stack_size, IV linked_to) {
	mthread* thread = mthread_alloc(linked_to);
#ifdef WIN32
	thread->handle = CreateThread(NULL,
								  (DWORD)stack_size,
								  run_thread,
								  (LPVOID)thread,
								  STACK_SIZE_PARAM_IS_A_RESERVATION,
								  &thread->thr);
#else
	int rc_stack_size = 0;
	int rc_thread_create = 0;

	S_block_most_signals(&thread->initial_sigmask);

	static pthread_attr_t attr;
	static int attr_inited = 0;
	static int attr_joinable = PTHREAD_CREATE_DETACHED;
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
	if (stack_size > 0) {
		rc_stack_size = pthread_attr_setstacksize(&attr, (size_t)stack_size);
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
	return thread;
}
