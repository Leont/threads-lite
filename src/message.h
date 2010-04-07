enum message_type { EMPTY = 0, STRING = 1, STORABLE = 2 };

typedef struct {
	enum message_type type;
	struct {
		char* ptr;
		STRLEN length;
	} string;
} message;

void S_message_clone(pTHX_ message* origin, message* clone);
#define message_clone(origin, clone) S_message_clone(aTHX_ origin, clone)

void S_message_push_stack(pTHX_ message*, U32 context);
#define message_push_stack(values, context) STMT_START { PUTBACK; S_message_push_stack(aTHX_ (values), context); SPAGAIN; } STMT_END

void S_message_pull_stack(pTHX_ message*);
#define message_pull_stack_pushed(message) STMT_START { PUTBACK; S_message_pull_stack(aTHX_ message); SPAGAIN; } STMT_END
#define message_pull_stack(message, offset) STMT_START { PUSHMARK(offset); message_pull_stack_pushed(message); } STMT_END

void S_message_store_value(pTHX_ message* message, SV* value);
#define message_store_value(message, value) S_message_store_value(aTHX_ message, value)

SV* S_message_load_value(pTHX_ message* message);
#define message_load_value(message) S_message_load_value(aTHX_ message)

