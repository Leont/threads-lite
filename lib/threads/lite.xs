#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "queue.h"
#include "mthread.h"
#include "resources.h"

message_queue* get_self(pTHX) {
	SV** self_sv = hv_fetch(PL_modglobal, "thread::lite::self", 18, FALSE);
	if (!self_sv)
		Perl_croak(aTHX_ "Can't find self thread object!");
	return &((mthread*)SvPV_nolen(*self_sv))->queue;
}
MODULE = threads::lite             PACKAGE = threads::lite

PROTOTYPES: DISABLED

BOOT:
	global_init();

SV*
_create(object)
	SV* object;
	CODE:
		mthread* thread = create_thread(65536);
		RETVAL = newRV_noinc(newSVuv(thread->thread_id));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));
	OUTPUT:
		RETVAL

void
_receive()
	PPCODE:
		message_queue* queue = get_self(aTHX);
		message message;
		queue_dequeue(queue, &message);
		message_push_stack(&message);
	
void
_receive_nb()
	PPCODE:
		message_queue* queue = get_self(aTHX);
		message message;
		if (queue_dequeue_nb(queue, &message))
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
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		UV thread_id = SvUV(SvRV(object));
		message message;
		PUSHMARK(MARK + 2);
		message_pull_stack(&message);
		thread_send(thread_id, &message);

void
join(object)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
			UV thread_id = SvUV(SvRV(object));
			thread_join(thread_id);

void
detach(object)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
			UV thread_id = SvUV(SvRV(object));
			thread_detach(thread_id);
