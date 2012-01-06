#define PERL_NO_GET_CONTEXT
#include "EXTERN.h"
#include "perl.h"

#ifdef __cplusplus
# define VOID2(T, P) static_cast<T>(P)
#else
# define VOID2(T, P) (P)
#endif

#define PTABLE_HASH(ptr) ((ptr >> 3) ^ (ptr >> (3 + 7)) ^ (ptr >> (3 + 17)))

STATIC UV generator() {
	static UV counter = 0;
	return counter++;
}

typedef struct thread_ent {
	struct thread_ent* next;
	UV           key;
	mthread*           val;
	int refcount;
} thread_ent;

typedef struct thread_table {
	thread_ent** ary;
	size_t       max;
	size_t       items;
} thread_table;

STATIC thread_table* S_thread_db_new(pTHX) {
	thread_table* t = VOID2(thread_table*, PerlMemShared_malloc(sizeof *t));
	t->max    = 15;
	t->items  = 0;
	t->ary    = VOID2(thread_ent **, PerlMemShared_calloc(t->max + 1, sizeof *t->ary));
	return t;
}
#define thread_db_new() S_thread_db_new(aTHX)

STATIC thread_ent* _thread_db_find(const thread_table* const t, const UV key) {
	thread_ent* ent;
	const UV hash = PTABLE_HASH(key);

	ent = t->ary[hash & t->max];
	for (; ent; ent = ent->next) {
		if (ent->key == key)
			return ent;
	}

	return NULL;
}

STATIC mthread* thread_db_fetch(const thread_table* const t, const UV key) {
	const thread_ent *const ent = _thread_db_find(t, key);
	return ent ? ent->val : NULL;
}

STATIC void _thread_db_split(pTHX_ thread_table* const t) {
	thread_ent **ary = t->ary;
	const size_t oldsize = t->max + 1;
	size_t newsize = oldsize * 2;
	size_t i;

	ary = VOID2(thread_ent **, PerlMemShared_realloc(ary, newsize * sizeof(*ary)));
	Zero(&ary[oldsize], newsize - oldsize, sizeof(*ary));
	t->max = --newsize;
	t->ary = ary;

	for (i = 0; i < oldsize; i++, ary++) {
		thread_ent **curentp, **entp, *ent;
		if (!*ary)
			continue;
		curentp = ary + oldsize;
		for (entp = ary, ent = *ary; ent; ent = *entp) {
			if ((newsize & PTABLE_HASH(ent->key)) != i) {
				*entp     = ent->next;
				ent->next = *curentp;
				*curentp  = ent;
				continue;
			} else
				entp = &ent->next;
		}
	}
}

STATIC void S_thread_db_store(pTHX_ thread_table* const t, const UV key, mthread* const val) {
	thread_ent *ent = _thread_db_find(t, key);

	if (ent) {
		mthread* oldval = ent->val;
		(void)(oldval);
		ent->val = val;
	} else if (val) {
		const size_t i = PTABLE_HASH(key) & t->max;
		ent = VOID2(thread_ent *, PerlMemShared_malloc(sizeof *ent));
		ent->key  = key;
		ent->val  = val;
		ent->next = t->ary[i];
		t->ary[i] = ent;
		ent->refcount = 1;
		t->items++;
		if (ent->next && t->items > t->max)
			_thread_db_split(aTHX_ t);
	}
}
#define thread_db_store(t, key, val) S_thread_db_store(aTHX_ t, key, val)

STATIC void S_thread_db_delete(pTHX_ thread_table * const t, const UV key) {
	thread_ent *prev, *ent;
	const size_t i = PTABLE_HASH(key) & t->max;

	prev = NULL;
	ent  = t->ary[i];
	for (; ent; prev = ent, ent = ent->next) {
		if (ent->key == key)
			break;
	}

	if (ent) {
		if (--ent->refcount)
			return;
		if (prev)
			prev->next = ent->next;
		else
			t->ary[i]  = ent->next;
		(void)(ent->val);
		PerlMemShared_free(ent);
	}
}
#define thread_db_delete(t, key) S_thread_db_delete(aTHX_ t, key)

STATIC void thread_db_incref(pTHX_ thread_table* const t, const UV key) {
	thread_ent* ent = _thread_db_find(t, key);
	if (ent)
		ent->refcount++;
}

STATIC void thread_db_decref(pTHX_ thread_table* const t, const UV key) {
	thread_ent* ent = _thread_db_find(t, key);
	if (ent)
		ent->refcount--;
}

STATIC void thread_walk(pTHX_ thread_table* const t, void (*cb)(pTHX_ thread_ent* ent, void* userdata), void *userdata) {
	if (t && t->items) {
		register thread_ent ** const array = t->ary;
		size_t i = t->max;
		do {
			thread_ent *entry;
			for (entry = array[i]; entry; entry = entry->next)
				if (entry->val)
					cb(aTHX_ entry, userdata);
		} while (i--);
	}
}

STATIC void thread_db_clear(pTHX_ thread_table * const t) {
	if (t && t->items) {
		register thread_ent ** const array = t->ary;
		size_t i = t->max;

		do {
			thread_ent *entry = array[i];
			while (entry) {
				thread_ent * const oentry = entry;
				void *val = oentry->val;
				entry = entry->next;
				(void)(val);
				PerlMemShared_free(oentry);
			}
			array[i] = NULL;
		} while (i--);

		t->items = 0;
	}
}

STATIC void thread_db_free(pTHX_ thread_table * const t) {
	if (!t)
		return;
	thread_db_clear(aTHX_ t);
	PerlMemShared_free(t->ary);
	PerlMemShared_free(t);
}

typedef struct queue_ent {
	struct queue_ent* next;
	UV           key;
	message_queue*           val;
	int refcount;
} queue_ent;

typedef struct queue_table {
	queue_ent** ary;
	size_t       max;
	size_t       items;
} queue_table;

STATIC queue_table* S_queue_db_new(pTHX) {
	queue_table* t = VOID2(queue_table*, PerlMemShared_malloc(sizeof *t));
	t->max    = 15;
	t->items  = 0;
	t->ary    = VOID2(queue_ent **, PerlMemShared_calloc(t->max + 1, sizeof *t->ary));
	return t;
}
#define queue_db_new() S_queue_db_new(aTHX)

STATIC queue_ent* _queue_db_find(const queue_table* const t, const UV key) {
	queue_ent* ent;
	const UV hash = PTABLE_HASH(key);

	ent = t->ary[hash & t->max];
	for (; ent; ent = ent->next) {
		if (ent->key == key)
			return ent;
	}

	return NULL;
}

STATIC message_queue* queue_db_fetch(const queue_table* const t, const UV key) {
	const queue_ent *const ent = _queue_db_find(t, key);
	return ent ? ent->val : NULL;
}

STATIC void _queue_db_split(pTHX_ queue_table* const t) {
	queue_ent **ary = t->ary;
	const size_t oldsize = t->max + 1;
	size_t newsize = oldsize * 2;
	size_t i;

	ary = VOID2(queue_ent **, PerlMemShared_realloc(ary, newsize * sizeof(*ary)));
	Zero(&ary[oldsize], newsize - oldsize, sizeof(*ary));
	t->max = --newsize;
	t->ary = ary;

	for (i = 0; i < oldsize; i++, ary++) {
		queue_ent **curentp, **entp, *ent;
		if (!*ary)
			continue;
		curentp = ary + oldsize;
		for (entp = ary, ent = *ary; ent; ent = *entp) {
			if ((newsize & PTABLE_HASH(ent->key)) != i) {
				*entp     = ent->next;
				ent->next = *curentp;
				*curentp  = ent;
				continue;
			} else
				entp = &ent->next;
		}
	}
}

STATIC void S_queue_db_store(pTHX_ queue_table* const t, const UV key, message_queue* const val) {
	queue_ent *ent = _queue_db_find(t, key);

	if (ent) {
		message_queue* oldval = ent->val;
		(void)(oldval);
		ent->val = val;
	} else if (val) {
		const size_t i = PTABLE_HASH(key) & t->max;
		ent = VOID2(queue_ent *, PerlMemShared_malloc(sizeof *ent));
		ent->key  = key;
		ent->val  = val;
		ent->next = t->ary[i];
		t->ary[i] = ent;
		ent->refcount = 1;
		t->items++;
		if (ent->next && t->items > t->max)
			_queue_db_split(aTHX_ t);
	}
}
#define queue_db_store(t, key, val) S_queue_db_store(aTHX_ t, key, val)

STATIC void S_queue_db_delete(pTHX_ queue_table * const t, const UV key) {
	queue_ent *prev, *ent;
	const size_t i = PTABLE_HASH(key) & t->max;

	prev = NULL;
	ent  = t->ary[i];
	for (; ent; prev = ent, ent = ent->next) {
		if (ent->key == key)
			break;
	}

	if (ent) {
		if (--ent->refcount)
			return;
		if (prev)
			prev->next = ent->next;
		else
			t->ary[i]  = ent->next;
		(void)(ent->val);
		PerlMemShared_free(ent);
	}
}
#define queue_db_delete(t, key) S_queue_db_delete(aTHX_ t, key)

STATIC void queue_db_incref(pTHX_ queue_table* const t, const UV key) {
	queue_ent* ent = _queue_db_find(t, key);
	if (ent)
		ent->refcount++;
}

STATIC void queue_db_decref(pTHX_ queue_table* const t, const UV key) {
	queue_ent* ent = _queue_db_find(t, key);
	if (ent)
		ent->refcount--;
}

STATIC void queue_walk(pTHX_ queue_table* const t, void (*cb)(pTHX_ queue_ent* ent, void* userdata), void *userdata) {
	if (t && t->items) {
		register queue_ent ** const array = t->ary;
		size_t i = t->max;
		do {
			queue_ent *entry;
			for (entry = array[i]; entry; entry = entry->next)
				if (entry->val)
					cb(aTHX_ entry, userdata);
		} while (i--);
	}
}

STATIC void queue_db_clear(pTHX_ queue_table * const t) {
	if (t && t->items) {
		register queue_ent ** const array = t->ary;
		size_t i = t->max;

		do {
			queue_ent *entry = array[i];
			while (entry) {
				queue_ent * const oentry = entry;
				void *val = oentry->val;
				entry = entry->next;
				(void)(val);
				PerlMemShared_free(oentry);
			}
			array[i] = NULL;
		} while (i--);

		t->items = 0;
	}
}

STATIC void queue_db_free(pTHX_ queue_table * const t) {
	if (!t)
		return;
	queue_db_clear(aTHX_ t);
	PerlMemShared_free(t->ary);
	PerlMemShared_free(t);
}

