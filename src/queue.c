#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "queue.h"

/*
 * struct message
 */

static SV* S_message_get_sv(pTHX_ message* message) {
	SV* stored = newSV_type(SVt_PV);
	SvPVX(stored) = message->string.ptr;
	SvLEN(stored) = SvCUR(stored) = message->string.length;
	SvPOK_only(stored);
	return stored;
}

#define message_get_sv(message) S_message_get_sv(aTHX_ message)

static void S_message_set_sv(pTHX_ message* message, SV* value, enum node_type type) {
	message->type = type;
	char* string = SvPV(value, message->string.length);
	message->string.ptr = savepvn(string, message->string.length);
}

#define message_set_sv(message, value, type) S_message_set_sv(aTHX_ message, value, type)

static void S_message_store_value(pTHX_ message* message, SV* value) {
	dSP;
	ENTER;
	SAVETMPS;
	sv_setiv(save_scalar(gv_fetchpv("Storable::Deparse", TRUE | GV_ADDMULTI, SVt_PV)), 1);
	PUSHMARK(SP);
	PUSHs(sv_2mortal(newRV_inc(value)));
	PUTBACK;
	call_pv("Storable::mstore", G_SCALAR);
	SPAGAIN;
	message_set_sv(message, POPs, STORABLE);
	FREETMPS;
	LEAVE;
}

#define message_store_value(message, value) S_message_store_value(aTHX_ message, value)

void S_message_pull_stack(pTHX_ message* message) {
	dSP; dMARK;
	if (SP == MARK) {
		if (!SvOK(*MARK) || SvROK(*MARK) || (SvPOK(*MARK) && SvUTF8(*MARK)))
			message_store_value(message, *MARK);
		else
			message_set_sv(message, *MARK, STRING);
	}
	else {
		SV* list = sv_2mortal((SV*)av_make(SP - MARK + 1, MARK));
		message_store_value(message, list);
	}
}

#define message_pull_stack(message) STMT_START { PUTBACK; S_message_pull_stack(aTHX_ message); SPAGAIN; } STMT_END

void S_message_push_stack(pTHX_ message* message) {
	dSP;

	switch(message->type) {
		case STRING:
			PUSHs(sv_2mortal(message_get_sv(message)));
			break;
		case STORABLE: {
			ENTER;
			sv_setiv(save_scalar(gv_fetchpv("Storable::Eval", TRUE | GV_ADDMULTI, SVt_PV)), 1);
			PUSHMARK(SP);
			XPUSHs(sv_2mortal(message_get_sv(message)));
			PUTBACK;
			call_pv("Storable::thaw", G_SCALAR);
			SPAGAIN;
			LEAVE;
			AV* values = (AV*)SvRV(POPs);

			if (GIMME_V == G_SCALAR) {
				SV** ret = av_fetch(values, 0, FALSE);
				PUSHs(ret ? *ret : &PL_sv_undef);
			}
			else if (GIMME_V == G_ARRAY) {
				UV count = av_len(values) + 1;
				Copy(AvARRAY(values), SP + 1, count, SV*);
				SP += count;
			}
			break;
		}
		default:
			Perl_croak(aTHX_ "Type %d is not yet implemented", message->type);
	}

	PUTBACK;
}

void S_message_clone(pTHX_ message* origin, message* clone) {
	clone->type = origin->type;
	switch (origin->type) {
		case EMPTY:
			break;
		case STRING:
		case STORABLE:
			clone->string.length = origin->string.length;
			clone->string.ptr = savepvn(origin->string.ptr, origin->string.length);
			break;
		default:
			warn("Unknown type in message\n");
	}
}

#define message_clone(origin, clone) S_message_clone(aTHX_ origin, clone)

void message_destroy(message* message) {
	switch (message->type) {
		case EMPTY:
			break;
		case STRING:
		case STORABLE:
			Safefree(message->string.ptr);
			Zero(message, 1, message);
			break;
		default:
			warn("Unknown type in message\n");
	}
}

/*
 * Message queues
 */

struct queue_node {
	message message;
	struct queue_node* next;
};

static void node_unshift(queue_node** position, queue_node* new_node) {
	new_node->next = *position;
	*position = new_node;
}

static queue_node* node_shift(queue_node** position) {
	queue_node* ret = *position;
	*position = (*position)->next;
	return ret;
}

static void node_push(queue_node** end, queue_node* new_node) {
	queue_node** cur = end;
	while(*cur)
		cur = &(*cur)->next;
	*end = *cur = new_node;
	new_node->next = NULL;
}

static void node_destroy(struct queue_node* current) {
	while (current != NULL) {
		struct queue_node* next = current->next;
		message_destroy(&current->message);
		Safefree(current);
		current = next;
	}
}

void queue_init(message_queue* queue) {
	MUTEX_INIT(&queue->mutex);
	COND_INIT(&queue->condvar);
}

void queue_enqueue(message_queue* queue, message* message_, perl_mutex* external_lock) {
	MUTEX_LOCK(&queue->mutex);
	if (external_lock)
		MUTEX_UNLOCK(external_lock);

	queue_node* new_entry;
	if (queue->reserve) {
		new_entry = node_shift(&queue->reserve);
	}
	else
		Newx(new_entry, 1, queue_node);

	Copy(message_, &new_entry->message, 1, message);
	new_entry->next = NULL;

	node_push(&queue->back, new_entry);
	if (queue->front == NULL)
		queue->front = queue->back;

	COND_SIGNAL(&queue->condvar);
	MUTEX_UNLOCK(&queue->mutex);
}

void S_queue_enqueue_copy(pTHX_ message_queue* queue, message* origin, perl_mutex* external_lock) {
	message clone;
	message_clone(origin, &clone);
	queue_enqueue(queue, &clone, external_lock);
}

void queue_dequeue(message_queue* queue, message* input) {
	MUTEX_LOCK(&queue->mutex);

	while (!queue->front)
		COND_WAIT(&queue->condvar, &queue->mutex);

	queue_node* front = node_shift(&queue->front);
	Copy(&front->message, input, 1, message);
	Zero(&front->message, 1, message);
	node_unshift(&queue->reserve, front);

	if (queue->front == NULL)
		queue->back = NULL;

	MUTEX_UNLOCK(&queue->mutex);
}

bool queue_dequeue_nb(message_queue* queue, message* input) {
	MUTEX_LOCK(&queue->mutex);

	if (queue->front) {
		queue_node* front = node_shift(&queue->front);
		Copy(&front->message, input, 1, message);
		Zero(&front->message, 1, message);
		node_unshift(&queue->reserve, front);

		if (queue->front == NULL)
			queue->back = NULL;

		MUTEX_UNLOCK(&queue->mutex);
		return TRUE;
	}
	else {
		MUTEX_UNLOCK(&queue->mutex);
		return FALSE;
	}
}

void queue_destroy(message_queue* queue) {
	MUTEX_LOCK(&queue->mutex);
	queue_node* next;
	node_destroy(queue->front);
	node_destroy(queue->reserve);
	COND_DESTROY(&queue->condvar);
	MUTEX_UNLOCK(&queue->mutex);
	MUTEX_DESTROY(&queue->mutex);
}
