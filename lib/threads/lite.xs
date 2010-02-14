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
	global_init(aTHX);

SV*
_create(class, options)
	SV* class;
	SV* options;
	CODE:
		mthread* thread = create_thread(options);
		RETVAL = newRV_noinc(newSVuv(thread->id));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));
	OUTPUT:
		RETVAL

SV*
_clone(class)
	SV* class;
	CODE:
		mthread* thread = clone_thread(aTHX_ 65536);
		RETVAL = newRV_noinc(newSVuv(thread->id));
		sv_bless(RETVAL, gv_stashpv("threads::lite::tid", FALSE));

void
_receive()
	PPCODE:
		mthread* thread = get_self();
		message message;
		queue_dequeue(&thread->queue, &message);
		message_push_stack(&message, GIMME_V);
	
void
_receive_nb()
	PPCODE:
		mthread* thread = get_self();
		message message;
		if (queue_dequeue_nb(&thread->queue, &message))
			message_push_stack(&message, GIMME_V);
		else
			XSRETURN_EMPTY;

void
_load_module(module)
	SV* module;
	CODE:
		load_module(PERL_LOADMOD_NOIMPORT, module, NULL, NULL);

SV* self()
	CODE:
		mthread* thread = get_self();
		SV** ret = hv_fetch(PL_modglobal, "threads::lite::self", 19, FALSE);
		RETVAL = SvREFCNT_inc(*ret);
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
		message_pull_stack(&message, MARK + 2);
		thread_send(thread_id, &message);

void monitor(object)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
		thread_add_listener(SvUV(SvRV(object)), get_self()->id);

MODULE = threads::lite             PACKAGE = threads::lite::queue

PROTOTYPES: DISABLED

SV*
new(class)
	SV* class;
	CODE:
		UV queue_id = queue_alloc();
		RETVAL = newRV_noinc(newSVuv(queue_id));
		sv_bless(RETVAL, gv_stashsv(class, FALSE));
	OUTPUT:
		RETVAL

void
enqueue(object, ...)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::queue"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a queue object\n");
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		UV queue_id = SvUV(SvRV(object));
		message message;
		message_pull_stack(&message, MARK + 2);
		queue_send(queue_id, &message);

void
dequeue(object)
	SV* object;
	PPCODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::queue"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a queue object\n");
		UV queue_id = SvUV(SvRV(object));
		message message;
//		queue_receive(queue, &message);
		message_push_stack(&message, GIMME_V);
