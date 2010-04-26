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
	return POPi;
}
#define deep_equals(entry, pattern) S_deep_equals(aTHX_ entry, pattern)

#define get_message_cache(thread) (thread)->cache

int S_match_mailbox(pTHX_ AV* cache, SV* criterion, U32 context) {
	dSP;
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
#define match_mailbox(cache, entry, context) S_match_mailbox(aTHX_ cache, entry, context)

void S_push_mailbox(pTHX_ AV* cache, SV* entry) {
	SV* tmp = newRV_noinc((SV*)entry);
	SvREFCNT_inc_nn(tmp);
	av_push(cache, tmp);
}
#define push_mailbox(cache, entry) S_push_mailbox(aTHX_ cache, entry)

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
	INIT:
		HV* real_options;
	PPCODE:
		PUTBACK;
		real_options = SvROK(options) && SvTYPE(SvRV(options)) == SVt_PVHV ? (HV*) SvRV(options) : (HV*)sv_2mortal((SV*)newHV());
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
		thread = get_self();
		AV* cache = get_message_cache(thread);
		ret_items = match_mailbox(cache, args, GIMME_V);
		SPAGAIN;
		if (ret_items)
			XSRETURN(ret_items);
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
			push_mailbox(cache, entry_sv);
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
	PPCODE:
		PUTBACK;
		match_mailbox(get_message_cache(get_self()), criterion, GIMME_V);
		SPAGAIN;

void
_push_mailbox(arg)
	SV* arg;
	CODE:
		push_mailbox(get_message_cache(get_self()), arg);

void
send_to(tid, ...)
	SV* tid;
	INIT:
		message message;
		UV thread_id;
	CODE:
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		thread_id = SvUV(tid);
		message_from_stack(&message, MARK + 1);
		thread_send(thread_id, &message);

MODULE = threads::lite             PACKAGE = threads::lite::tid

PROTOTYPES: DISABLED

void
send(object, ...)
	SV* object;
	INIT:
		message message;
		UV thread_id;
	CODE:
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		thread_id = SvUV(SvRV(object));
		message_from_stack(&message, MARK + 1);
		thread_send(thread_id, &message);

void monitor(object)
	SV* object;
	CODE:
		thread_add_listener(aTHX, SvUV(SvRV(object)), get_self()->id);

MODULE = threads::lite             PACKAGE = threads::lite::queue

PROTOTYPES: DISABLED

SV*
new(class)
	SV* class;
	INIT:
		UV queue_id;
	CODE:
		queue_id = queue_alloc();
		RETVAL = newRV_noinc(newSVuv(queue_id));
		sv_bless(RETVAL, gv_stashsv(class, FALSE));
	OUTPUT:
		RETVAL

void
enqueue(object, ...)
	SV* object;
	INIT:
		message message;
		UV queue_id;
	CODE:
		if (items == 1)
			Perl_croak(aTHX_ "Can't send an empty list\n");
		queue_id = SvUV(SvRV(object));
		message_from_stack(&message, MARK + 1);
		queue_send(queue_id, &message);

void
dequeue(object)
	SV* object;
	INIT:
		message message;
		UV queue_id;
	PPCODE:
		queue_id = SvUV(SvRV(object));
		queue_receive(queue_id, &message);
		message_to_stack(&message, GIMME_V);

void
dequeue_nb(object)
	SV* object;
	INIT:
		message message;
		UV queue_id;
	PPCODE:
		queue_id = SvUV(SvRV(object));
		if (queue_receive_nb(queue_id, &message))
			message_to_stack(&message, GIMME_V);
		else
			XSRETURN_EMPTY;
