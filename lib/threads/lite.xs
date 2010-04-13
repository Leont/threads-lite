#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "message.h"
#include "queue.h"
#include "mthread.h"
#include "resources.h"

int S_deep_equals(pTHX_ SV* entry, SV* pattern) {
	dSP;
	PUSHMARK(SP);
	XPUSHs(entry);
	XPUSHs(pattern);
	PUTBACK;
	call_pv("threads::lite::_deep_equals", G_SCALAR);
	SPAGAIN;
	SV* ret = POPs;
	return SvTRUE(ret);
}
#define deep_equals(entry, pattern) S_deep_equals(aTHX_ entry, pattern)

int S_match_mailbox(pTHX_ SV* criterion, U32 context) {
	dSP;
	AV* cache = (AV*)*hv_fetch(PL_modglobal, "threads::lite::message_cache", 28, 0);
	SV** cache_raw = AvARRAY(cache);
	SSize_t last = av_len(cache);
	SSize_t counter;
	for(counter = 0; counter <= last; counter++) {
		if (deep_equals(cache_raw[counter], criterion)) {
			SV* ret = cache_raw[counter];
			Move(cache_raw + counter + 1, cache_raw + counter, last - counter, SV*);
			AvFILLp(cache)--;
			return _return_elements((AV*)SvRV(ret), context);
		}
	}
	return 0;
}
#define match_mailbox(entry, context) S_match_mailbox(aTHX_ entry, context)

void S_push_mailbox(pTHX_ SV* entry) {
	AV* cache = (AV*)*hv_fetch(PL_modglobal, "threads::lite::message_cache", 28, 0);
	SV* tmp = newRV_noinc((SV*)entry);
	SvREFCNT_inc_nn(tmp);
	av_push(cache, tmp);
}
#define push_mailbox(entry) S_push_mailbox(aTHX_ entry)

int S_return_elements(pTHX_ AV* values, U32 context) {
	dSP;
	UV count;
	if (context == G_SCALAR) {
		SV** ret = av_fetch(values, 0, FALSE);
		PUSHs(ret ? *ret : &PL_sv_undef);
		count = 1;
	}
	else if (context == G_ARRAY) {
		count = av_len(values) + 1;
		EXTEND(SP, count);
		Copy(AvARRAY(values), SP + 1, count, SV*);
		SP += count;
	}
	PUTBACK;
	return count;
}

#define return_elements(entry, context) S_return_elements(aTHX_ entry, context)

MODULE = threads::lite             PACKAGE = threads::lite

PROTOTYPES: DISABLED

BOOT:
	global_init(aTHX);

SV*
spawn(options, startup)
	SV* options;
	SV* startup;
	PPCODE:
		PUTBACK;
		HV* real_options = SvROK(options) && SvTYPE(SvRV(options)) == SVt_PVHV ? (HV*) SvRV(options) : (HV*)sv_2mortal((SV*)newHV());
		create_push_threads(real_options, startup);
		SPAGAIN;


void
receive(...)
	INIT:
		SV* args;
		int ret_items;
		mthread* thread;
	PPCODE:
		args = newRV_noinc((SV*)av_make(items, MARK + 1));
		PUTBACK;
		ret_items = match_mailbox(args, GIMME_V);
		SPAGAIN;
		if (ret_items)
			XSRETURN(ret_items);
		thread = get_self();
		while (1) {
			message message;
			AV* entry;
			SV* entry_sv;
			queue_dequeue(&thread->queue, &message, NULL);
			message_to_array(&message, &entry);
			entry_sv = sv_2mortal(newRV_inc((SV*)entry));
			if (items == 0 || deep_equals(entry_sv, args)) {
				PUTBACK;
				return_elements(entry, GIMME_V);
				SPAGAIN;
				break;
			}
			push_mailbox(entry_sv);
		}

void
_receive()
	PPCODE:
		mthread* thread = get_self();
		message message;
		queue_dequeue(&thread->queue, &message, NULL);
		message_to_stack(&message, GIMME_V);
	
void
_receive_nb()
	PPCODE:
		mthread* thread = get_self();
		message message;
		if (queue_dequeue_nb(&thread->queue, &message, NULL))
			message_to_stack(&message, GIMME_V);
		else
			XSRETURN_EMPTY;

SV* self()
	CODE:
		mthread* thread = get_self();
		SV** ret = hv_fetch(PL_modglobal, "threads::lite::self", 19, FALSE);
		RETVAL = SvREFCNT_inc(*ret);
	OUTPUT:
		RETVAL

void
_return_elements(arg)
	SV* arg;
	PPCODE:
		PUTBACK;
		return_elements((AV*)SvRV(arg), GIMME_V);
		SPAGAIN;

void
_match_mailbox(criterion)
	SV* criterion;
	INIT:
		int ret;
	PPCODE:
		PUTBACK;
		match_mailbox(criterion, GIMME_V);
		SPAGAIN;

void
_push_mailbox(arg)
	SV* arg;
	CODE:
		push_mailbox(arg);

void
send_to(tid, ...)
	SV* tid;
	CODE:
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		UV thread_id = SvUV(tid);
		message message;
		message_from_stack(&message, MARK + 1);
		thread_send(thread_id, &message);

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
		message_from_stack(&message, MARK + 1);
		thread_send(thread_id, &message);

void monitor(object)
	SV* object;
	CODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::tid"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a thread object\n");
		thread_add_listener(aTHX, SvUV(SvRV(object)), get_self()->id);

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
		message_from_stack(&message, MARK + 1);
		queue_send(queue_id, &message);

void
dequeue(object)
	SV* object;
	PPCODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::queue"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a queue object\n");
		UV queue_id = SvUV(SvRV(object));
		message message;
		queue_receive(queue_id, &message);
		message_to_stack(&message, GIMME_V);

void
dequeue_nb(object)
	SV* object;
	PPCODE:
		if (!sv_isobject(object) || !sv_derived_from(object, "threads::lite::queue"))
			Perl_croak(aTHX_ "Something is very wrong, this is not a queue object\n");
		UV queue_id = SvUV(SvRV(object));
		message message;
		if (queue_receive_nb(queue_id, &message))
			message_to_stack(&message, GIMME_V);
		else
			XSRETURN_EMPTY;
