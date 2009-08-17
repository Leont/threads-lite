#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "queue.h"
#include "mthread.h"
#include "resources.h"

mthread* get_self(pTHX) {
	SV** self_sv = hv_fetch(PL_modglobal, "thread::lite::self", 18, FALSE);
	if (!self_sv)
		Perl_croak(aTHX_ "Can't find self thread object!");
	return (mthread*)SvPV_nolen(*self_sv);
}

MODULE = threads::lite             PACKAGE = threads::lite

PROTOTYPES: DISABLED

BOOT:
	global_init(aTHX);

SV*
_create(object, monitor)
	SV* object;
	SV* monitor;
	CODE:
		UV id = SvTRUE(monitor) ? get_self(aTHX)->thread_id : -1;
		mthread* thread = create_thread(65536, id);
		RETVAL = newRV_noinc(newSVuv(thread->thread_id));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));
	OUTPUT:
		RETVAL

void
_receive()
	PPCODE:
		mthread* thread = get_self(aTHX);
		message message;
		queue_dequeue(&thread->queue, &message);
		message_push_stack(&message);
	
void
_receive_nb()
	PPCODE:
		mthread* thread = get_self(aTHX);
		message message;
		if (queue_dequeue_nb(&thread->queue, &message))
			 message_push_stack(&message);
		else
			XSRETURN_EMPTY;

void
_load_module(module)
	SV* module;
	CODE:
		load_module(PERL_LOADMOD_NOIMPORT, module, NULL, NULL);

SV* self()
	CODE:
		mthread* thread = get_self(aTHX);
		RETVAL = newRV_noinc(newSVuv(thread->thread_id));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));
	OUTPUT:
		RETVAL

MODULE = threads::lite             PACKAGE = threads::lite::tid

PROTOTYPES: DISABLED

void
send(object, ...)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		UV thread_id = SvUV(SvRV(object));
		message message;
		PUSHMARK(MARK + 2);
		message_pull_stack(&message);
		thread_send(thread_id, &message);

void monitor(object)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
		thread_add_listener(SvUV(SvRV(object)), get_self(aTHX)->thread_id);
