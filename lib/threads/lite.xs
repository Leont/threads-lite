#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "queue.h"
#include "mthread.h"
#include "resources.h"

MODULE = threads::lite             PACKAGE = threads::lite

PROTOTYPES: DISABLED

BOOT:
	global_init();

SV*
_create(object)
	SV* object;
	CODE:
		mthread* thread = create_thread(65536);
		RETVAL = newRV_noinc(newSVuv(PTR2UV(thread)));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));
	OUTPUT:
		RETVAL

void
_receive()
	PPCODE:
		SV** self_sv = hv_fetch(PL_modglobal, "thread::lite::self", 18, FALSE);
		if (!self_sv)
			Perl_croak(aTHX_ "Can't find self thread object!");
		mthread* thread = (mthread*)SvPV_nolen(*self_sv);
		message message;
		queue_dequeue(thread->queue, &message);
		message_push_stack(&message);
	
void
_receive_nb()
	PPCODE:
		SV** self_sv = hv_fetch(PL_modglobal, "thread::lite::self", 18, FALSE);
		if (!self_sv)
			Perl_croak(aTHX_ "Can't find self thread object!");
		mthread* thread = (mthread*)SvPV_nolen(*self_sv);
		message message;
		if (queue_dequeue_nb(thread->queue, &message))
			 message_push_stack(&message);
		else
			XSRETURN_EMPTY;

void
_load_module(module)
	SV* module;
	CODE:
		load_module(PERL_LOADMOD_NOIMPORT, module, NULL, NULL);

MODULE = threads::lite             PACKAGE = threads::lite::tid

PROTOTYPES: DISABLED

void
send(object, ...)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a magic thread object\n");
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		message_queue* queue = (INT2PTR(mthread*, SvUV(SvRV(object))))->queue;
		message message;
		PUSHMARK(MARK + 2);
		message_pull_stack(&message);
		queue_enqueue(queue, &message);

